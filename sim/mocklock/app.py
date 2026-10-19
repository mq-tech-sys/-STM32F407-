"""
mocklock.app —— 启动入口

  python -m mocklock.app                    # 云端 + 本地，两条链路一起开
  python -m mocklock.app --mode local       # 只跑本地 WebSocket，不连巴法云
  python -m mocklock.app --probe            # 只做连通性诊断，不启动模拟器
  python -m mocklock.app --probe --appid xx --secret yy

默认参数与固件 HARDWARE/net/bemfa_mqtt.h 保持一致（主题 lock000 / 私钥作为 ClientID），
所以"在模拟器上通了"等价于"换成真板子也通"。

注意：巴法云的主题名和私钥是一对一的。用这串私钥订阅 lock001 会收到
SUBACK 0x80（拒绝），订阅 lock000 才是 0x00（成功）—— 2026-10-19 实测。
"""

import argparse
import os
import sys
import threading
import time
import webbrowser

from . import lock as lock_mod
from .localbroker import LocalBroker
from .mqtt import MqttSession, TcpTransport, WsTransport, open_transport
from .web import Console, Hub

# 与 HARDWARE/net/bemfa_mqtt.h 对齐
DEFAULT_UID = "28eccdb351e4bf84ad1390c143846844"
DEFAULT_TOPIC = "lock000"
DEFAULT_HOST = "bemfa.com"
DEFAULT_PORT = 9501
WSS_URL = "wss://bemfa.com:9504/wss"

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class CloudLink:
    """一条到巴法云的连接，断了自动重连。"""

    def __init__(self, url, client_id, topic, username=None, password=None,
                 keepalive=60, lock=None, hub=None, label="巴法云"):
        self.url = url
        self.client_id = client_id
        self.topic = topic
        self.username = username
        self.password = password
        self.keepalive = keepalive
        self.lock = lock
        self.hub = hub
        self.label = label
        self.session = None
        self.connected = False
        self.error = ""
        self._stop = threading.Event()
        self._thread = None

    # ---------------------------------------------------------- 对外
    def start(self):
        if not self.url:
            return self
        self._thread = threading.Thread(target=self._run, name="cloud", daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self.session:
            self.session.close()

    def publish(self, msg):
        if self.session and self.connected:
            return self.session.publish(self.topic + "/set", msg)
        return False

    # ---------------------------------------------------------- 内部
    def _log(self, kind, text):
        if self.hub:
            self.hub.add_log(kind, "[%s] %s" % (self.label, text))

    def _run(self):
        while not self._stop.is_set():
            try:
                self._once()
            except Exception as e:                       # noqa: BLE001
                self.error = str(e)
                self._log("error", "连接失败：%s" % e)
            self.connected = False
            if self._stop.is_set():
                return
            self._log("info", "5 秒后重连…")
            self._stop.wait(5)

    def _once(self):
        transport, kind = open_transport(self.url)
        done = threading.Event()

        def on_event(evt, info):
            if evt == "connect":
                self.connected = True
                self.error = ""
                self._log("info", "%s 已连接（%s，ClientID %s）" % (self.label, kind, self.client_id))
            elif evt == "suback":
                if info == 0x80:
                    # 巴法云用 0x80 表示「这个私钥无权订阅这个主题」
                    self.error = "主题与私钥不匹配"
                    self._log("error",
                              "订阅 %s 被拒绝（SUBACK 0x80）：私钥和主题名不是同一个主题，"
                              "设备收不到任何命令" % self.topic)
                elif info not in (0, 1, 2):
                    self.error = "订阅失败 rc=%s" % info
                    self._log("error", "订阅 %s 失败，返回码 %s" % (self.topic, info))
                else:
                    self._log("info", "已订阅主题 %s（rc=%s），远程控制就绪" % (self.topic, info))
                    # 和固件一致：收到 SUBACK 之后再上报一次当前状态
                    if self.lock:
                        self.lock.on_online()
            elif evt == "disconnect":
                self.connected = False
                self._log("warn", "连接断开：%s" % (info or ""))
                done.set()
            elif evt == "error":
                self.error = str(info)
                self._log("error", str(info))
                done.set()

        def on_message(topic, payload):
            self._log("rx", "[云端] %s" % payload)
            if self.lock:
                self.lock.handle(payload, echo=False)

        self.session = MqttSession(
            transport,
            client_id=self.client_id,
            username=self.username,
            password=self.password,
            keepalive=self.keepalive,
            topics=[self.topic],
            on_message=on_message,
            on_event=on_event,
        ).start()

        while not done.is_set() and not self._stop.is_set():
            done.wait(0.5)
        self.session.close()


# ---------------------------------------------------------------- probe

def probe(args):
    """诊断：设备侧 TCP 通不通、小程序侧 WSS 通不通、两边能不能互相收发。"""
    ok_all = True
    print("=" * 62)
    print(" 门锁链路诊断  topic = %s" % args.topic)
    print("=" * 62)

    # ---- 1. 设备侧：私钥直连明文 MQTT ----
    print("\n[1/3] 设备侧  tcp://%s:%d  ClientID=私钥" % (args.host, args.port))
    dev = _probe_connect(
        lambda: TcpTransport(args.host, args.port),
        client_id=args.uid, username=None, password=None,
        topic=args.topic, timeout=12,
    )
    if dev["ok"]:
        print("      OK  CONNACK rc=0 + SUBACK rc=%s，主题 %s 可订阅、私钥有效"
              % (dev["subrc"], args.topic))
    else:
        ok_all = False
        print("      失败  %s" % dev["msg"])

    # ---- 2. 小程序侧：WSS + appID/secretKey ----
    print("\n[2/3] 小程序侧  %s  ClientID=%s" % (WSS_URL, args.client_id))
    if not (args.appid and args.secret):
        print("      跳过  没提供 --appid / --secret")
        print("      （小程序必须用 appID + secretKey 登录；私钥只能给设备端用）")
        _summary(args, dev, None, None)
        return 1 if not dev["ok"] else 0

    app = _probe_connect(
        lambda: WsTransport(WSS_URL),
        client_id=args.client_id, username=args.appid, password=args.secret,
        topic=args.topic, timeout=15,
    )
    if app["ok"]:
        print("      OK  WSS + MQTT 握手成功，说明小程序这条链路本身没问题")
    else:
        ok_all = False
        print("      失败  %s" % app["msg"])

    # ---- 3. 双向路由 ----
    route = None
    if dev["ok"] and app["ok"]:
        print("\n[3/3] 主题路由：小程序发 ping → 设备回 pong")
        route = _probe_route(args)
        if route["ok"]:
            print("      OK  双向都通（%d ms）" % route["ms"])
        else:
            ok_all = False
            print("      失败  %s" % route["msg"])
    else:
        print("\n[3/3] 主题路由：两端没同时连上，跳过")

    _summary(args, dev, app, route)
    return 0 if ok_all else 1


def _probe_connect(make_transport, client_id, username, password, topic, timeout):
    result = {"ok": False, "msg": "", "subrc": None}
    done = threading.Event()

    def on_event(kind, info):
        # 注意：CONNACK 成功不代表能用 —— 巴法云对"主题和私钥不是一对"的情况
        # 是在 SUBACK 里回 0x80，必须等订阅结果出来才能下结论。
        if kind == "connect":
            result["connack"] = True
        elif kind == "suback":
            result["subrc"] = info
            if info == 0x80:
                result["msg"] = ("订阅 %s 被拒绝（SUBACK 0x80）：主题名和私钥不是同一个主题。"
                                 "去巴法云控制台核对主题名。" % topic)
                done.set()
            elif info in (0, 1, 2):
                result["ok"] = True
                done.set()
            else:
                result["msg"] = "子订阅失败，SUBACK rc=%s" % info
                done.set()
        elif kind == "error":
            result["msg"] = str(info)
            done.set()
        elif kind == "disconnect" and not result["ok"]:
            hint = ""
            if result.get("connack"):
                hint = ("（CONNACK 已收到、SUBACK 没等到：多半是同一个私钥重复登录被巴法云顶号，"
                        "先关掉正在跑的模拟器/真机再诊断）")
            result["msg"] = str(info) + hint
            done.set()

    try:
        t = make_transport()
    except Exception as e:                                # noqa: BLE001
        result["msg"] = "建链失败：%s" % e
        return result

    s = MqttSession(t, client_id=client_id, username=username, password=password,
                    keepalive=60, topics=[topic], on_event=on_event).start()
    done.wait(timeout)
    if not done.is_set():
        result["msg"] = ("超时 %d 秒：CONNACK %s，SUBACK %s"
                         % (timeout,
                            "已收到" if result.get("connack") else "未收到",
                            "未收到"))
    s.close()
    return result


def _probe_route(args):
    """两个连接：小程序身份订阅 topic，设备身份发 topic/set，看前者是否收到"""
    out = {"ok": False, "ms": 0, "msg": ""}
    got = threading.Event()
    dev_ready = threading.Event()
    app_ready = threading.Event()

    def app_msg(topic, payload):
        if payload.strip() == "pong":
            got.set()

    def dev_msg(topic, payload):
        if payload.strip() == "ping":
            dev_session.publish(args.topic + "/set", "pong")

    def dev_event(kind, info):
        if kind == "connect":
            dev_ready.set()

    def app_event(kind, info):
        if kind == "connect":
            app_ready.set()

    try:
        dev_session = MqttSession(TcpTransport(args.host, args.port), args.uid,
                                  keepalive=60, topics=[args.topic],
                                  on_message=dev_msg, on_event=dev_event).start()
        app_session = MqttSession(WsTransport(WSS_URL), args.client_id,
                                  username=args.appid, password=args.secret,
                                  keepalive=60, topics=[args.topic],
                                  on_message=app_msg, on_event=app_event).start()
    except Exception as e:                                # noqa: BLE001
        out["msg"] = "建链失败：%s" % e
        return out

    if not (dev_ready.wait(10) and app_ready.wait(10)):
        out["msg"] = "两端未能同时在线"
        dev_session.close()
        app_session.close()
        return out

    time.sleep(0.6)          # 等订阅生效
    t0 = time.time()
    app_session.publish(args.topic + "/set", "ping")
    ok = got.wait(8)
    out["ms"] = int((time.time() - t0) * 1000)
    if ok:
        out["ok"] = True
    else:
        out["msg"] = "8 秒内没收到回包（主题名是否两端一致？）"
    dev_session.close()
    app_session.close()
    return out


def _summary(args, dev, app, route):
    print("\n" + "-" * 62)
    print(" 结论")
    print("-" * 62)
    print(" 小程序设置页应填：")
    print("   服务器   %s" % WSS_URL)
    print("   主题名   %s" % args.topic)
    print("   ClientID %s   （不要填成私钥，否则会把设备顶下线）" % args.client_id)
    print("   用户名   %s" % (args.appid or "<巴法云 appID>"))
    print("   密码     %s" % ("******" if args.secret else "<巴法云 secretKey>"))
    if not (args.appid and args.secret):
        print("\n 提示：appID / secretKey 在巴法云控制台「连接密钥」页复制。")
    if route and route["ok"]:
        print("\n 整条链路（小程序 ↔ 巴法云 ↔ 设备）已验证通畅。")
    print("")


# ---------------------------------------------------------------- 主程序

def main(argv=None):
    ap = argparse.ArgumentParser(description="STM32 智能门锁 · Windows 虚拟设备")
    ap.add_argument("--mode", choices=["both", "cloud", "local"], default="both")
    ap.add_argument("--topic", default=DEFAULT_TOPIC, help="巴法云主题名")
    ap.add_argument("--uid", default=DEFAULT_UID, help="设备私钥（当 ClientID 用）")
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--tls", action="store_true", help="用 9503 TLS 端口")
    ap.add_argument("--client-id", default="wxmp_lock01", help="小程序侧 ClientID（诊断用）")
    ap.add_argument("--appid", default="", help="巴法云 appID（诊断/自定义 ClientID 时必填）")
    ap.add_argument("--secret", default="", help="巴法云 secretKey")
    ap.add_argument("--web-port", type=int, default=8848)
    ap.add_argument("--local-port", type=int, default=9001)
    ap.add_argument("--state", default=os.path.join(HERE, "sim_state.json"))
    ap.add_argument("--reset", action="store_true", help="启动前恢复演示数据")
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--probe", action="store_true")
    args = ap.parse_args(argv)

    if args.probe:
        return probe(args)

    if args.reset and os.path.exists(args.state):
        os.remove(args.state)

    print("=" * 62)
    print(" 智能门锁 · 虚拟设备（Windows Demo）")
    print("=" * 62)

    hub_ref = {}
    ctx = {"cloud": None, "local": None}

    def log(kind, text):
        h = hub_ref.get("hub")
        if h:
            h.add_log(kind, text)
        else:
            print("[%s] %s" % (kind, text))

    offline_warned = {"v": False}

    def send_to_peer(msg):
        """设备发出的每条消息，同时投到云和本地两条链路（哪条有客户端就走哪条）。"""
        sent = False
        c = ctx["cloud"]
        if c and c.publish(msg):
            sent = True
        b = ctx["local"]
        if b and b.client_count():
            b.broadcast(args.topic + "/set", msg)
            log("tx", "[本地] " + msg)
            sent = True
        if not sent:
            # 只提醒一次，别让 fp_list 的每一行都刷屏
            if not offline_warned["v"]:
                log("warn", "还没有客户端连进来，这条应答只留在控制台。等小程序连上会自动同步。")
                offline_warned["v"] = True
        else:
            offline_warned["v"] = False

    lock = lock_mod.VirtualLock(send=send_to_peer, on_log=log, state_path=args.state)

    if args.mode in ("both", "cloud"):
        scheme = "tls" if args.tls else "tcp"
        ctx["cloud"] = CloudLink(
            "%s://%s:%d" % (scheme, args.host, args.port),
            client_id=args.uid,
            topic=args.topic,
            username=args.appid or None,
            password=args.secret or None,
            lock=lock,
            hub=None,
            label="巴法云",
        )

    if args.mode in ("both", "local"):
        ctx["local"] = LocalBroker(
            port=args.local_port,
            log=lambda t: log("info", "[本地] " + t),
            on_message=lambda topic, payload: lock.handle(payload, echo=False),
        )

    cloud, local = ctx["cloud"], ctx["local"]

    hub = Hub(lock,
              cloud_info=lambda: ({"connected": cloud.connected, "error": cloud.error}
                                  if cloud else None),
              local_info=lambda: {"clients": local.client_count() if local else 0},
              topic=args.topic,
              local_url="ws://127.0.0.1:%d" % args.local_port,
              local_enabled=local is not None)
    hub_ref["hub"] = hub

    if cloud:
        cloud.hub = hub
        cloud.start()
    if local:
        local.start()

    console = Console(hub, port=args.web_port).start()

    print(" 主题       : %s" % args.topic)
    if cloud:
        print(" 巴法云     : %s:%d  (ClientID = 设备私钥)" % (args.host, args.port))
    else:
        print(" 巴法云     : 未启用（--mode %s）" % args.mode)
    if local:
        print(" 本地 WS    : ws://127.0.0.1:%d" % args.local_port)
    print(" 控制台     : http://127.0.0.1:%d" % args.web_port)
    print("=" * 62)
    print(" 提示：控制台里能直接开锁、增删指纹/卡，效果等同小程序下发命令。")
    print(" 按 Ctrl+C 退出。\n")

    url = "http://127.0.0.1:%d" % args.web_port
    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n正在退出…")
    finally:
        if cloud:
            cloud.stop()
        if local:
            local.stop()
        console.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
