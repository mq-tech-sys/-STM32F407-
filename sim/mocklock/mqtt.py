"""
mocklock.mqtt —— 零依赖的 MQTT 3.1.1 客户端（TCP / TLS / WebSocket）+ 报文编解码。

只实现门锁项目用得到的 6 种报文：CONNECT / CONNACK / SUBSCRIBE / SUBACK /
PUBLISH / PINGREQ+PINGRESP，够用且行为和小程序 utils/mqtt.js 一致。
"""

import base64
import os
import socket
import ssl
import struct
import threading
import time
from urllib.parse import urlparse

CONNECT = 1
CONNACK = 2
PUBLISH = 3
SUBSCRIBE = 8
SUBACK = 9
PINGREQ = 12
PINGRESP = 13
DISCONNECT = 14

CONNACK_HINT = {
    1: "协议版本不支持",
    2: "ClientID 不合法",
    3: "服务端不可用",
    4: "用户名或密码错误（要 appID / secretKey）",
    5: "未授权（私钥或密钥错）",
}


class MqttError(Exception):
    pass


# --------------------------------------------------------------- 报文编码

def enc_len(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out.append(b)
        if not n:
            return bytes(out)


def enc_str(s):
    b = s.encode("utf-8") if isinstance(s, str) else bytes(s)
    return struct.pack(">H", len(b)) + b


def packet(header, body=b""):
    return bytes([header]) + enc_len(len(body)) + body


# --------------------------------------------------------------- 传输层

class TcpTransport:
    """裸 TCP，巴法云明文 9501 / TLS 9503 都走这里。"""

    def __init__(self, host, port, tls=False, insecure=False, timeout=10):
        raw = socket.create_connection((host, port), timeout=timeout)
        if tls:
            ctx = ssl.create_default_context()
            if insecure:
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
            raw = ctx.wrap_socket(raw, server_hostname=host)
        raw.settimeout(None)
        self.sock = raw

    def send(self, data):
        self.sock.sendall(data)

    def recv(self):
        try:
            return self.sock.recv(4096)
        except OSError:
            return b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class WsTransport:
    """MQTT over WebSocket（客户端），对应小程序的 wx.connectSocket 那条链路。"""

    def __init__(self, url, timeout=10, insecure=False, subprotocol="mqtt"):
        u = urlparse(url)
        if not u.hostname:
            raise MqttError("URL 不合法: %s" % url)
        secure = u.scheme in ("wss", "https")
        host = u.hostname
        port = u.port or (443 if secure else 80)
        path = u.path or "/"
        if u.query:
            path += "?" + u.query

        raw = socket.create_connection((host, port), timeout=timeout)
        if secure:
            ctx = ssl.create_default_context()
            if insecure:
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
            raw = ctx.wrap_socket(raw, server_hostname=host)

        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            "GET %s HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n" % (path, host, port, key)
        )
        if subprotocol:
            req += "Sec-WebSocket-Protocol: %s\r\n" % subprotocol
        req += "\r\n"
        raw.sendall(req.encode())

        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = raw.recv(4096)
            if not chunk:
                raise MqttError("WebSocket 握手时连接被关闭")
            buf += chunk
        head, rest = buf.split(b"\r\n\r\n", 1)
        status = head.split(b"\r\n")[0].decode("latin1")
        if " 101" not in status:
            raise MqttError("WebSocket 握手失败: %s" % status)

        raw.settimeout(None)
        self.sock = raw
        self._rest = rest
        self.url = url
        self.subprotocol = subprotocol

    # -- 内部 --
    def _read_exact(self, n):
        data = self._rest
        while len(data) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            data += chunk
        out, self._rest = data[:n], data[n:]
        return out

    def _send_frame(self, opcode, data):
        n = len(data)
        hdr = bytes([0x80 | opcode])
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(hdr + mask + masked)

    # -- 对外 --
    def send(self, data):
        self._send_frame(0x2, data)

    def recv(self):
        payload = b""
        while True:
            hdr = self._read_exact(2)
            if not hdr:
                return b""
            b1, b2 = hdr[0], hdr[1]
            fin = b1 & 0x80
            opcode = b1 & 0x0F
            masked = b2 & 0x80
            ln = b2 & 0x7F
            if ln == 126:
                ext = self._read_exact(2)
                if not ext:
                    return b""
                ln = struct.unpack(">H", ext)[0]
            elif ln == 127:
                ext = self._read_exact(8)
                if not ext:
                    return b""
                ln = struct.unpack(">Q", ext)[0]

            mask = self._read_exact(4) if masked else None
            data = self._read_exact(ln) if ln else b""
            if data is None:
                return b""
            if mask:
                data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))

            if opcode == 0x8:            # close
                self.close()
                return b""
            if opcode == 0x9:            # ping -> pong
                self._send_frame(0xA, data)
                continue
            if opcode == 0xA:            # pong
                continue

            payload += data
            if fin:
                return payload

    def close(self):
        try:
            self._send_frame(0x8, b"")
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


# --------------------------------------------------------------- MQTT 会话

class MqttSession:
    """
    一条 MQTT 连接。构造后调用 start()；断开时 on_event('disconnect', 原因)。
    回调：
        on_event(kind, info)  kind = connect / disconnect / suback / error / pingresp
        on_message(topic, text)
    """

    def __init__(self, transport, client_id, username=None, password=None,
                 keepalive=60, topics=(), on_message=None, on_event=None):
        self.transport = transport
        self.client_id = client_id
        self.username = username or None
        self.password = password or None
        self.keepalive = keepalive
        self.topics = list(topics)
        self.on_message = on_message
        self.on_event = on_event

        self.connected = False
        self.connack_rc = None
        self.suback_rc = None
        self._buf = bytearray()
        self._tx = threading.Lock()
        self._closed = False
        self._mid = 1
        self._ping_stop = threading.Event()
        self.started_at = time.time()

    # -- 内部 --
    def _emit(self, kind, info=None):
        if self.on_event:
            try:
                self.on_event(kind, info)
            except Exception:
                pass

    def _send(self, data):
        with self._tx:
            if self._closed:
                return False
            try:
                self.transport.send(data)
                return True
            except OSError:
                self._shutdown("发送失败")
                return False

    def _next_mid(self):
        self._mid = (self._mid % 65535) + 1
        return self._mid

    def _connect_packet(self):
        body = bytearray()
        body += enc_str("MQTT")
        body.append(4)                       # 协议级别 3.1.1
        flags = 0x02                         # clean session
        if self.username:
            flags |= 0x80
        if self.password:
            flags |= 0x40
        body.append(flags)
        body += struct.pack(">H", self.keepalive)
        body += enc_str(self.client_id)
        if self.username:
            body += enc_str(self.username)
        if self.password:
            body += enc_str(self.password)
        return packet(CONNECT << 4, bytes(body))

    # -- 对外 --
    def start(self):
        self._send(self._connect_packet())
        threading.Thread(target=self._read_loop, name="mqtt-rx", daemon=True).start()
        threading.Thread(target=self._ping_loop, name="mqtt-ping", daemon=True).start()
        return self

    def subscribe(self, topic, qos=0):
        mid = self._next_mid()
        body = struct.pack(">H", mid) + enc_str(topic) + bytes([qos])
        return self._send(packet((SUBSCRIBE << 4) | 0x02, body))

    def publish(self, topic, payload):
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        if not self.connected:
            return False
        return self._send(packet(PUBLISH << 4, enc_str(topic) + payload))

    def close(self):
        if not self._closed:
            try:
                self._send(packet(DISCONNECT << 4))
            except Exception:
                pass
        self._shutdown("本端主动关闭")

    # -- 收包 --
    def _read_loop(self):
        while not self._closed:
            try:
                chunk = self.transport.recv()
            except Exception as e:                     # noqa: BLE001
                self._shutdown("读取异常: %s" % e)
                return
            if not chunk:
                self._shutdown("连接已被对端关闭")
                return
            self._buf += chunk
            while True:
                pkt = self._take_one()
                if pkt is None:
                    break
                self._dispatch(*pkt)

    def _take_one(self):
        buf = self._buf
        if len(buf) < 2:
            return None
        remain, mult, n, i = 0, 1, 0, 1
        while i < len(buf) and n < 4:
            b = buf[i]
            remain += (b & 0x7F) * mult
            mult *= 128
            n += 1
            i += 1
            if not (b & 0x80):
                break
            if n == 4:
                del buf[0]
                return None
        if i == len(buf) and (buf[i - 1] & 0x80):
            return None
        header_len = 1 + n
        total = header_len + remain
        if len(buf) < total:
            return None
        body = bytes(buf[header_len:total])
        typ = buf[0] >> 4
        flags = buf[0] & 0x0F
        del buf[:total]
        return typ, flags, body

    def _dispatch(self, typ, flags, body):
        if typ == CONNACK:
            rc = body[1] if len(body) > 1 else 255
            self.connack_rc = rc
            if rc == 0:
                self.connected = True
                self._emit("connect", rc)
                for t in self.topics:
                    self.subscribe(t)
            else:
                self._emit("error", "CONNACK rc=%d（%s）" % (rc, CONNACK_HINT.get(rc, "未知")))
                self._shutdown("认证失败 rc=%d" % rc)
            return

        if typ == SUBACK:
            # body = 报文标识符(2) + 每个主题一个返回码
            rc = body[2] if len(body) > 2 else -1
            self.suback_rc = rc
            self._emit("suback", rc)
            return

        if typ == PINGRESP:
            self._emit("pingresp", None)
            return

        if typ == PUBLISH:
            if len(body) < 2:
                return
            tlen = (body[0] << 8) | body[1]
            off = 2
            topic = body[off:off + tlen].decode("utf-8", "replace")
            off += tlen
            if ((flags & 0x06) >> 1) == 1 and len(body) >= off + 2:
                off += 2
            payload = body[off:].decode("utf-8", "replace")
            if self.on_message:
                try:
                    self.on_message(topic, payload)
                except Exception:
                    pass

    def _ping_loop(self):
        interval = max(5, int(self.keepalive / 2))
        while not self._ping_stop.wait(interval):
            if self._closed:
                return
            self._send(packet(PINGREQ << 4))

    def _shutdown(self, reason):
        if self._closed and not self.connected:
            return
        was = self.connected
        self._closed = True
        self.connected = False
        self._ping_stop.set()
        try:
            self.transport.close()
        except Exception:
            pass
        if was:
            self._emit("disconnect", reason)


def open_transport(url):
    """
    统一入口：接受 host:port / tcp:// / tls:// / ws:// / wss:// / bemfa 简写。
    返回 (transport, 描述)
    """
    if "://" not in url:
        url = "tcp://" + url
    u = urlparse(url)
    scheme = u.scheme.lower()
    if scheme in ("ws", "wss"):
        return WsTransport(url), "WebSocket(%s)" % scheme
    if scheme in ("tls", "mqtts", "ssl"):
        return TcpTransport(u.hostname, u.port or 8883, tls=True), "TLS"
    return TcpTransport(u.hostname, u.port or 1883, tls=False), "TCP"
