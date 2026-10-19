"""
mocklock.localbroker —— 极简 MQTT over WebSocket 服务端（只在 127.0.0.1 上监听）。

用途：微信开发者工具里勾上「不校验合法域名」后，把小程序的服务器地址改成
ws://127.0.0.1:9001 ，就能完全绕开巴法云和公网，纯本地跑通全链路。
只实现门锁用到的报文，不支持 QoS1/2、不支持保留消息、不支持通配符以外的特性。

注意：真机预览不能连 ws://（微信只允许 wss），本地模式只用于开发者工具。
"""

import base64
import hashlib
import socket
import socketserver
import struct
import threading

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def _enc_len(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out.append(b)
        if not n:
            return bytes(out)


def _enc_str(s):
    b = s.encode("utf-8") if isinstance(s, str) else bytes(s)
    return struct.pack(">H", len(b)) + b


class _WsConn:
    """一条 WebSocket 连接上的 MQTT 字节流。"""

    def __init__(self, sock, addr):
        self.sock = sock
        self.addr = addr
        self.buf = b""          # WebSocket 层残留字节
        self.mqtt_buf = b""     # 已解帧、还没凑成完整 MQTT 报文的字节
        self.client_id = ""
        self.subs = set()
        self.alive = True
        self.lock = threading.Lock()

    # ---------------------------------------------------- WebSocket 帧
    def _recv_exact(self, n):
        data = self.buf
        while len(data) < n:
            try:
                chunk = self.sock.recv(4096)
            except OSError:
                return None
            if not chunk:
                return None
            data += chunk
        out, self.buf = data[:n], data[n:]
        return out

    def handshake(self):
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                return False
            head += chunk
        header, rest = head.split(b"\r\n\r\n", 1)
        lines = header.decode("latin1").split("\r\n")
        if not lines or "websocket" not in header.decode("latin1").lower():
            return False
        key = ""
        proto = ""
        for ln in lines[1:]:
            if ":" not in ln:
                continue
            k, v = ln.split(":", 1)
            k = k.strip().lower()
            if k == "sec-websocket-key":
                key = v.strip()
            elif k == "sec-websocket-protocol":
                proto = v.strip()
        if not key:
            return False
        accept = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode()).digest()
        ).decode()
        resp = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n" % accept
        )
        if proto:
            # 回显客户端要的协议名（mqtt / mqttv3.1 等），部分客户端会校验
            resp += "Sec-WebSocket-Protocol: %s\r\n" % proto.split(",")[0].strip()
        resp += "\r\n"
        self.sock.sendall(resp.encode())
        self.buf = rest
        return True

    def recv_frame(self):
        hdr = self._recv_exact(2)
        if not hdr:
            return None, None
        b1, b2 = hdr[0], hdr[1]
        fin = b1 & 0x80
        opcode = b1 & 0x0F
        masked = b2 & 0x80
        ln = b2 & 0x7F
        if ln == 126:
            ext = self._recv_exact(2)
            if not ext:
                return None, None
            ln = struct.unpack(">H", ext)[0]
        elif ln == 127:
            ext = self._recv_exact(8)
            if not ext:
                return None, None
            ln = struct.unpack(">Q", ext)[0]
        mask = self._recv_exact(4) if masked else b""
        data = self._recv_exact(ln) if ln else b""
        if data is None:
            return None, None
        if masked and mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        return opcode, data, fin

    def send_frame(self, opcode, data):
        with self.lock:
            n = len(data)
            hdr = bytes([0x80 | opcode])
            if n < 126:
                hdr += bytes([n])
            elif n < 65536:
                hdr += bytes([126]) + struct.pack(">H", n)
            else:
                hdr += bytes([127]) + struct.pack(">Q", n)
            try:
                self.sock.sendall(hdr + data)
            except OSError:
                self.alive = False

    def send_mqtt(self, data):
        self.send_frame(0x2, data)

    # ---------------------------------------------------- MQTT 报文
    def recv_mqtt(self):
        """取出一个完整 MQTT 报文；必要时跨 WS 帧拼接"""
        while True:
            pkt = self._parse_one()
            if pkt:
                return pkt
            opcode, data, fin = self.recv_frame()
            if opcode is None:
                return None
            if opcode == 0x8:                       # close
                return None
            if opcode == 0x9:                       # ping
                self.send_frame(0xA, data)
                continue
            if opcode == 0xA:                       # pong
                continue
            self.mqtt_buf += data

    def _parse_one(self):
        stream = self.mqtt_buf
        if len(stream) < 2:
            return None
        remain, mult, n, i = 0, 1, 0, 1
        while i < len(stream) and n < 4:
            b = stream[i]
            remain += (b & 0x7F) * mult
            mult *= 128
            n += 1
            i += 1
            if not (b & 0x80):
                break
        total = 1 + n + remain
        if len(stream) < total:
            return None
        body = stream[1 + n:total]
        self.mqtt_buf = stream[total:]
        return stream[0] >> 4, stream[0] & 0x0F, body

    def push_mqtt(self, data):
        """把多个 MQTT 报文塞进缓冲，用 WS 二进制帧发出去"""
        self.send_mqtt(data)


class _Broker(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, addr, handler, broker):
        self.broker = broker
        super().__init__(addr, handler)


class LocalBroker:
    """极简主题总线：订阅 lock000 的客户端能收到发往 lock000/set 的消息。"""

    def __init__(self, host="127.0.0.1", port=9001, log=None, on_message=None):
        self.host = host
        self.port = port
        self.log = log or (lambda *a: None)
        self.on_message = on_message          # 收到客户端发来的消息时回调（设备侧逻辑挂这里）
        self._conns = set()
        self._lock = threading.Lock()
        self._server = None

    def start(self):
        handler = _make_handler(self)
        self._server = _Broker((self.host, self.port), handler, self)
        threading.Thread(target=self._server.serve_forever, name="ws-broker", daemon=True).start()
        self.log("本地 MQTT/WebSocket 已监听 ws://%s:%d" % (self.host, self.port))
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()

    # -- 客户端表 --
    def add(self, conn):
        with self._lock:
            self._conns.add(conn)

    def remove(self, conn):
        with self._lock:
            self._conns.discard(conn)

    def subscribe(self, conn, topic):
        conn.subs.add(topic)

    def client_count(self):
        with self._lock:
            return len(self._conns)

    def broadcast(self, topic, payload):
        """设备侧主动发消息（sender=None 表示不排除任何订阅者）"""
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        return self.publish(None, topic, payload)

    @staticmethod
    def _match(pub_topic, sub_topic):
        if sub_topic == "#":
            return True
        if sub_topic.endswith("/#"):
            return pub_topic.startswith(sub_topic[:-2])
        return pub_topic in (sub_topic, sub_topic + "/set") or sub_topic == pub_topic + "/set"

    def publish(self, sender, topic, payload):
        """把消息转发给订阅了该主题的其它客户端"""
        frame = bytes([0x30]) + _enc_len(len(_enc_str(topic)) + len(payload)) + _enc_str(topic) + payload
        with self._lock:
            targets = [c for c in self._conns
                       if c is not sender and any(self._match(topic, s) for s in c.subs)]
        for c in targets:
            c.send_mqtt(frame)
        return len(targets)


def _make_handler(broker):
    class Handler(socketserver.BaseRequestHandler):
        def handle(self):
            conn = _WsConn(self.request, self.client_address)
            if not conn.handshake():
                return
            broker.add(conn)
            broker.log("本地连接建立：%s" % (self.client_address[0],))
            try:
                while conn.alive:
                    pkt = conn.recv_mqtt()
                    if not pkt:
                        break
                    typ, flags, body = pkt
                    if typ == 1:                                # CONNECT
                        conn.client_id = _parse_client_id(body)
                        conn.send_mqtt(bytes([0x20, 0x02, 0x00, 0x00]))
                        broker.log("本地客户端 CONNECT：%s" % conn.client_id)
                    elif typ == 8:                              # SUBSCRIBE
                        mid = body[0:2]
                        off, topics = 2, []
                        while off + 2 <= len(body):
                            tlen = (body[off] << 8) | body[off + 1]
                            off += 2
                            topic = body[off:off + tlen].decode("utf-8", "replace")
                            off += tlen + 1
                            topics.append(topic)
                            broker.subscribe(conn, topic)
                        conn.send_mqtt(bytes([0x90]) + _enc_len(2 + len(topics))
                                       + mid + bytes([0x00] * len(topics)))
                        broker.log("本地订阅：%s" % ",".join(topics))
                    elif typ == 3:                              # PUBLISH
                        tlen = (body[0] << 8) | body[1]
                        topic = body[2:2 + tlen].decode("utf-8", "replace")
                        payload = body[2 + tlen:]
                        n = broker.publish(conn, topic, payload)
                        broker.log("本地转发 %s -> %d 个客户端：%s"
                                   % (topic, n, payload.decode("utf-8", "replace")))
                        if broker.on_message:
                            try:
                                broker.on_message(topic, payload.decode("utf-8", "replace"))
                            except Exception as e:              # noqa: BLE001
                                broker.log("处理本地命令失败：%s" % e)
                    elif typ == 12:                             # PINGREQ
                        conn.send_mqtt(bytes([0xD0, 0x00]))
                    elif typ == 14:                             # DISCONNECT
                        break
            except OSError:
                pass
            finally:
                broker.remove(conn)
                broker.log("本地连接断开：%s" % (self.client_address[0],))
                try:
                    conn.sock.close()
                except OSError:
                    pass

    return Handler


def _parse_client_id(body):
    try:
        off = 2 + ((body[0] << 8) | body[1]) + 1 + 1 + 2   # 协议名 + 版本 + flags + keepalive
        ln = (body[off] << 8) | body[off + 1]
        return body[off + 2:off + 2 + ln].decode("utf-8", "replace")
    except (IndexError, UnicodeDecodeError):
        return "?"
