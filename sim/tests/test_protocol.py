"""
协议自测：把 HARDWARE/net/remote_cmd.c 的每条分支都在 PC 上跑一遍。

  python tests/test_protocol.py
  python -m unittest discover -s tests -v
"""

import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mocklock import lock as lock_mod                     # noqa: E402
from mocklock.localbroker import LocalBroker              # noqa: E402
from mocklock.lock import CMD_LIST, VirtualLock           # noqa: E402
from mocklock.mqtt import MqttSession, WsTransport        # noqa: E402


class Recorder:
    def __init__(self):
        self.lines = []

    def __call__(self, msg):
        self.lines.append(msg)

    def take(self):
        out, self.lines = self.lines, []
        return out


def make_lock(state_path=None):
    rec = Recorder()
    lk = VirtualLock(send=rec, state_path=state_path)
    lk.reset(notify=False)
    rec.take()
    return lk, rec


class TestRemoteCmd(unittest.TestCase):
    def setUp(self):
        self.lock, self.rec = make_lock()
        lock_mod.ENROLL_SECONDS = 0.05

    # ---------------------------------------------------------- 开关锁
    def test_unlock(self):
        self.lock.handle("unlock")
        self.assertEqual(self.rec.take(), ["ack=unlock", "state=unlock"])
        self.assertFalse(self.lock.locked)

    def test_auto_relock(self):
        """固件开锁 2 秒后舵机自动回位并上报 state=lock，模拟器必须一样。

        之前模拟器只把 locked 置 False，永远不收回，于是小程序显示
        “2 秒后自动闭锁”，设备却一直是已开锁。"""
        old = lock_mod.AUTO_LOCK_SECONDS
        lock_mod.AUTO_LOCK_SECONDS = 0.2
        try:
            self.lock.handle("unlock")
            self.assertEqual(self.rec.take(), ["ack=unlock", "state=unlock"])
            self.assertFalse(self.lock.locked)
            time.sleep(0.45)
            self.assertTrue(self.lock.locked, "2 秒后应自动上锁")
            self.assertEqual(self.rec.take(), ["state=lock"], "自动上锁要主动上报")
        finally:
            lock_mod.AUTO_LOCK_SECONDS = old

    def test_pwd_unlock_also_auto_relocks(self):
        """密码开锁在固件里走 fire_servo(1)，同样 2 秒后回锁。

        漏掉这段时，小程序发 pwd:123456 会收到 ack=pwd_ok + state=unlock，
        然后永远停在"已开锁"——用户实测踩到的就是这个。"""
        old = lock_mod.AUTO_LOCK_SECONDS
        lock_mod.AUTO_LOCK_SECONDS = 0.2
        try:
            self.lock.handle("pwd:" + self.lock.password)
            self.assertEqual(self.rec.take(), ["ack=pwd_ok", "state=unlock"])
            self.assertFalse(self.lock.locked)
            time.sleep(0.45)
            self.assertTrue(self.lock.locked, "密码开锁也要 2 秒后自动上锁")
            self.assertEqual(self.rec.take(), ["state=lock"])
        finally:
            lock_mod.AUTO_LOCK_SECONDS = old

    def test_close_cancels_auto_relock(self):
        """开锁后马上上锁，不该再冒出一条重复的 state=lock。"""
        old = lock_mod.AUTO_LOCK_SECONDS
        lock_mod.AUTO_LOCK_SECONDS = 0.2
        try:
            self.lock.handle("unlock")
            self.rec.take()
            self.lock.handle("close")
            self.rec.take()
            time.sleep(0.4)
            self.assertEqual(self.rec.take(), [], "已上锁就不该再自动上报一次")
        finally:
            lock_mod.AUTO_LOCK_SECONDS = old

    def test_close_and_aliases(self):
        self.lock.handle("on")
        self.assertFalse(self.lock.locked)
        self.rec.take()
        self.lock.handle("off")
        self.assertEqual(self.rec.take(), ["ack=close", "state=lock"])
        self.assertTrue(self.lock.locked)

    def test_state(self):
        self.lock.handle("state")
        self.assertEqual(self.rec.take(), ["state=lock"])

    def test_reply_echo_is_ignored(self):
        """自己（或另一台同主题设备）的回包被投回来时，不能当成命令再回一次。

        两个模拟器同时在线时，A 的 help=... 会到 B 那里，B 若回 err=unknown + help=，
        又会回到 A，瞬间指数级互刷。这条用例锁住"不回应答"的行为。"""
        for line in ("ack=unlock", "state=lock", "num=fp,3", "help=unlock,close",
                     "err=unknown", "fp=1", "fpend=3", "card=0,A1B2C3D4",
                     "cardend=2", "pong"):
            self.lock.handle(line)
        self.assertEqual(self.rec.take(), [], "回投的应答不该产生任何回复")
        self.assertEqual(self.lock._echo_dropped, 10)

    # ---------------------------------------------------------- 数量
    def test_counts(self):
        self.lock.handle("fp_num")
        self.assertEqual(self.rec.take(), ["num=fp,3", "num=card,2"])

    def test_online_report(self):
        self.lock.on_online()
        self.assertEqual(self.rec.take(), ["state=lock", "num=fp,3", "num=card,2", "ack=online"])

    # ---------------------------------------------------------- 指纹列表
    def test_fp_list(self):
        self.lock.handle("fp_list")
        self.assertEqual(self.rec.take(), ["fp=1", "fp=2", "fp=5", "fpend=3"])

    def test_fp_list_empty(self):
        self.lock.handle("fp_del")
        self.rec.take()
        self.lock.handle("fp_list")
        self.assertEqual(self.rec.take(), ["fpend=0"])

    # ---------------------------------------------------------- 指纹删除
    def test_fp_del_one(self):
        self.lock.handle("fp_del:2")
        self.assertEqual(self.rec.take(), ["ack=fp_del_ok"])
        self.lock.handle("fp_list")
        self.assertEqual(self.rec.take(), ["fp=1", "fp=5", "fpend=2"])

    def test_fp_del_missing(self):
        self.lock.handle("fp_del:99")
        self.assertEqual(self.rec.take(), ["ack=fp_del_fail"])

    def test_fp_del_bad_format(self):
        for bad in ("fp_del:", "fp_del:abc", "fp_del:1x", "fp_del:-1", "fp_del:123456"):
            self.lock.handle(bad)
            self.assertEqual(self.rec.take(), ["ack=fp_del_bad"], bad)

    def test_fp_del_all(self):
        self.lock.handle("fp_del")
        self.assertEqual(self.rec.take(), ["ack=fp_del"])
        self.assertEqual(self.lock.snapshot()["fps"], [])

    # ---------------------------------------------------------- 卡列表
    def test_card_list_format(self):
        self.lock.handle("card_list")
        self.assertEqual(self.rec.take(), ["card=0,A1B2C3D4", "card=1,1A2B3C4D", "cardend=2"])

    def test_card_del_shifts_index(self):
        self.lock.handle("card_del:0")
        self.assertEqual(self.rec.take(), ["ack=card_del_ok"])
        self.lock.handle("card_list")
        self.assertEqual(self.rec.take(), ["card=0,1A2B3C4D", "cardend=1"])

    def test_card_del_bad_and_fail(self):
        self.lock.handle("card_del:")
        self.assertEqual(self.rec.take(), ["ack=card_del_bad"])
        self.lock.handle("card_del:9")
        self.assertEqual(self.rec.take(), ["ack=card_del_fail"])

    def test_card_max_ten(self):
        lk, rec = make_lock()
        lk.cards = [bytes([i, i, i, i]) for i in range(10)]
        lk.handle("card_list")
        lines = rec.take()
        self.assertEqual(len([x for x in lines if x.startswith("card=")]), 10)
        self.assertEqual(lines[-1], "cardend=10")

    # ---------------------------------------------------------- 密码
    def test_pwd_ok(self):
        self.lock.handle("pwd:123456")
        self.assertEqual(self.rec.take(), ["ack=pwd_ok", "state=unlock"])
        self.assertFalse(self.lock.locked)

    def test_pwd_fail(self):
        self.lock.handle("pwd:000000")
        self.assertEqual(self.rec.take(), ["ack=pwd_fail"])
        self.assertTrue(self.lock.locked)

    def test_pwd_set_and_use(self):
        self.lock.handle("pwd_set:654321")
        self.assertEqual(self.rec.take(), ["ack=pwd_set_ok"])
        self.lock.handle("pwd:654321")
        self.assertEqual(self.rec.take(), ["ack=pwd_ok", "state=unlock"])

    def test_pwd_set_bad(self):
        for bad in ("pwd_set:", "pwd_set:abcdef", "pwd_set:1234567"):
            self.lock.handle(bad)
            self.assertEqual(self.rec.take(), ["ack=pwd_set_bad"], bad)
        self.assertEqual(self.lock.password, "123456")

    # ---------------------------------------------------------- 其它
    def test_ping_pong(self):
        self.lock.handle("ping")
        self.assertEqual(self.rec.take(), ["pong"])

    def test_help(self):
        self.lock.handle("help")
        self.assertEqual(self.rec.take(), ["help=" + CMD_LIST])

    def test_unknown(self):
        self.lock.handle("whatever")
        self.assertEqual(self.rec.take(), ["err=unknown", "help=" + CMD_LIST])

    def test_blank_ignored(self):
        self.lock.handle("   ")
        self.assertEqual(self.rec.take(), [])

    # ---------------------------------------------------------- 忙碌
    def test_busy_blocks_fp_list(self):
        self.lock.handle("fp_add")
        self.assertEqual(self.rec.take(), ["ack=fp_add"])
        self.lock.handle("fp_list")
        self.assertEqual(self.rec.take(), ["err=busy"])
        self.lock.handle("fp_del:1")
        self.assertEqual(self.rec.take(), ["err=busy"])

    def test_enroll_assigns_first_free(self):
        """删掉 #1 后录入，应补到最小的空位 #0，且不能覆盖 #2 #5"""
        self.lock.handle("fp_del:1")
        self.rec.take()
        self.lock.handle("fp_add")
        self.rec.take()
        time.sleep(0.35)
        ids = self.lock.snapshot()["fps"]
        self.assertEqual(ids, [0, 2, 5], "应补空位，而不是覆盖已有模板")
        self.assertIn("num=fp,3", self.rec.take())

    def test_card_enroll(self):
        self.lock.handle("card_add")
        self.rec.take()
        time.sleep(0.35)
        self.assertEqual(len(self.lock.snapshot()["cards"]), 3)

    # ---------------------------------------------------------- 持久化
    def test_state_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.json")
            lk, _ = make_lock(path)
            lk.handle("fp_del:1")
            lk.handle("pwd_set:4321")
            lk2 = VirtualLock(send=lambda m: None, state_path=path)
            snap = lk2.snapshot()
            self.assertEqual(snap["fps"], [2, 5])
            self.assertEqual(snap["password"], "4321")


class TestLocalBroker(unittest.TestCase):
    """端到端：两个 WebSocket 客户端通过内置 broker 互发，模拟小程序 ↔ 设备"""

    @classmethod
    def setUpClass(cls):
        cls.broker = LocalBroker(port=19001, log=lambda t: None).start()
        time.sleep(0.2)

    @classmethod
    def tearDownClass(cls):
        cls.broker.stop()

    def test_route_set_to_subscriber(self):
        url = "ws://127.0.0.1:19001"
        got = []
        ev = threading.Event()

        # 设备端：收到的命令交给 VirtualLock，锁的应答再发回 <topic>/set
        def on_dev(topic, payload):
            lk.handle(payload, echo=False)

        lk = VirtualLock(send=lambda m: dev.publish("lock001/set", m))
        dev = MqttSession(WsTransport(url), "dev-1", topics=["lock001"],
                          on_message=on_dev).start()

        def on_app(topic, payload):
            got.append((topic, payload))
            if payload.startswith("fpend="):
                ev.set()

        app = MqttSession(WsTransport(url), "app-1", topics=["lock001"],
                          on_message=on_app).start()
        time.sleep(0.4)

        self.assertTrue(app.publish("lock001/set", "fp_list"))
        self.assertTrue(ev.wait(3), "小程序没收到设备应答")
        dev.close()
        app.close()

        self.assertEqual([p for _, p in got], ["fp=1", "fp=2", "fp=5", "fpend=3"])
        self.assertEqual(got[0][0], "lock001/set")


if __name__ == "__main__":
    unittest.main(verbosity=2)
