"""
mocklock.lock —— 虚拟门锁：只做 STM32 固件 HARDWARE/net/remote_cmd.c 的协议行为，
不含任何硬件逻辑。命令解析、应答文本、错误码、处理顺序都与 C 代码逐条对齐，
因此在 Windows 上测通的小程序，换到真机同样能通。

与固件的唯一差别（为了能在一个控制台里看出"录入成功了"）：
  fp_add  / card_add  是模拟录入，2 秒后自动生成一条记录并补发 num=fp,n / num=card,n。
  真机是在 OLED 上按手指 / 贴卡，不会发这两条。小程序对未知行是"原样显示"，无副作用。
"""

import json
import os
import random
import threading
import time

FP_CAPACITY = 100          # AS608 默认 100 个模板
CARD_MAX = 10              # g_card_list 长度
PASSWORD_MAX_LEN = 6
ENROLL_SECONDS = 2.0       # 模拟"请按手指 / 请贴卡"的耗时
AUTO_LOCK_SECONDS = 2.0    # 对应固件 task_servo() 里开锁后的 vTaskDelay(2000)

CMD_LIST = (
    "unlock,close,state,fp_add,fp_del,fp_del:<id>,fp_list,fp_num,"
    "card_add,card_del,card_del:<idx>,card_list,card_num,"
    "pwd:<6>,pwd_set:<6>,ping"
)

# 设备自己会发出的应答前缀。同一个主题上只要有两台设备（比如模拟器开了两份），
# 或者 broker 把消息回投给发布者，这些应答就会被当成命令收回来；
# 一旦对它们回 err=unknown + help=，两台设备会互刷成死循环（日志暴涨、刷爆巴法云）。
REPLY_PREFIXES = ("ack=", "state=", "num=", "help=", "err=",
                  "fp=", "fpend=", "card=", "cardend=")

# 首次启动的演示数据，方便一进小程序就有东西可删
DEFAULT_FPS = [1, 2, 5]
DEFAULT_CARDS = ["A1B2C3D4", "1A2B3C4D"]


def _is_uint(s, limit=99999):
    if not s or not s.isdigit():
        return None
    v = int(s)
    return v if v <= limit else None


def _uid_to_bytes(uid):
    uid = uid.upper().replace(" ", "")
    return bytes.fromhex(uid)


def _uid_to_text(b):
    return "".join("%02X" % x for x in b)


class VirtualLock:
    def __init__(self, send=None, on_log=None, on_change=None, state_path=None):
        self._send_raw = send or (lambda s: None)
        self._log = on_log or (lambda kind, text: None)
        self._on_change = on_change or (lambda: None)
        self.state_path = state_path
        self._mutex = threading.RLock()

        self.locked = True
        self.password = "123456"
        self.fps = []
        self.cards = []              # list[bytes]
        self.busy = False
        self.busy_what = ""
        self._busy_gen = 0
        self.last_rx = 0.0
        self.online_reported = False
        self._echo_dropped = 0
        self._auto_lock_gen = 0

        if not self._load():
            self.reset(notify=False)

    # ------------------------------------------------------------ 状态

    def reset(self, notify=True):
        self._cancel_auto_lock()
        with self._mutex:
            self.locked = True
            self.password = "123456"
            self.fps = list(DEFAULT_FPS)
            self.cards = [_uid_to_bytes(u) for u in DEFAULT_CARDS]
            self.busy = False
            self._busy_gen += 1
            self._save()
        self._log("info", "已恢复演示数据：指纹 #1 #2 #5，卡 2 张，密码 123456")
        if notify:
            self.report_state()
            self.report_counts()
            self._on_change()

    # ------------------------------------------------------------ 自动闭锁

    def _cancel_auto_lock(self):
        """作废正在等待的自动闭锁（收到 close 或重置数据时调）。"""
        self._auto_lock_gen += 1

    def _schedule_auto_lock(self):
        """开锁 2 秒后自动回位上锁，对应固件 task_servo() 里的 vTaskDelay(2000)。"""
        self._auto_lock_gen += 1
        gen = self._auto_lock_gen

        def worker():
            time.sleep(AUTO_LOCK_SECONDS)
            with self._mutex:
                # 期间又被操作过（重开、上锁、重置）就放弃这次
                if gen != self._auto_lock_gen or self.locked:
                    return
                self.locked = True
                self._save()
            self.send("state=lock")
            self._log("info", "2 秒到，舵机回位：门锁已自动上锁")
            self._on_change()

        threading.Thread(target=worker, name="auto-lock", daemon=True).start()

    def snapshot(self):
        with self._mutex:
            return {
                "locked": self.locked,
                "password": self.password,
                "fps": sorted(self.fps),
                "cards": [{"idx": i, "uid": _uid_to_text(c)} for i, c in enumerate(self.cards)],
                "busy": self.busy,
                "busy_what": self.busy_what,
                "last_rx": self.last_rx,
            }

    def _save(self):
        if not self.state_path:
            return
        data = {
            "locked": self.locked,
            "password": self.password,
            "fps": sorted(self.fps),
            "cards": [_uid_to_text(c) for c in self.cards],
        }
        try:
            with open(self.state_path, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except OSError:
            pass

    def _load(self):
        if not self.state_path or not os.path.exists(self.state_path):
            return False
        try:
            with open(self.state_path, "r", encoding="utf-8") as f:
                d = json.load(f)
            self.locked = bool(d.get("locked", True))
            self.password = str(d.get("password", "123456"))
            self.fps = [int(x) for x in d.get("fps", [])]
            self.cards = [_uid_to_bytes(u) for u in d.get("cards", [])]
            return True
        except (OSError, ValueError, KeyError):
            return False

    # ------------------------------------------------------------ 收发

    def send(self, msg):
        self._log("tx", msg)
        try:
            self._send_raw(msg)
        except Exception as e:                      # noqa: BLE001
            self._log("error", "发送失败: %s" % e)

    def report_state(self):
        self.send("state=unlock" if not self.locked else "state=lock")

    def report_counts(self):
        with self._mutex:
            n_fp, n_card = len(self.fps), len(self.cards)
        self.send("num=fp,%u" % n_fp)
        self.send("num=card,%u" % n_card)

    def on_online(self):
        """对应固件 remote_cmd_on_online()：刚连上先把现状报一遍"""
        self.report_state()
        self.report_counts()
        self.send("ack=online")
        self.online_reported = True
        self._on_change()

    # ------------------------------------------------------------ 忙碌

    def _begin_busy(self, what):
        with self._mutex:
            if self.busy:
                return None
            self.busy = True
            self.busy_what = what
            self._busy_gen += 1
            gen = self._busy_gen
        self._log("info", "%s 开始，%d 秒内设备处于忙碌状态" % (what, ENROLL_SECONDS))
        self._on_change()
        return gen

    def _end_busy(self, gen):
        with self._mutex:
            if gen != self._busy_gen or not self.busy:
                return
            self.busy = False
            self.busy_what = ""
        self._log("info", "设备恢复空闲")
        self._on_change()

    # ------------------------------------------------------------ 命令分发

    def handle(self, line, echo=True, from_console=False):
        line = (line or "").strip()
        if not line:
            return
        # 只有真正从链路（MQTT / BLE）进来的命令才算"对端活跃"，
        # 控制台自己点按钮不该刷新这个时间。
        if not from_console:
            self.last_rx = time.time()
        if echo:
            self._log("rx", line)

        # 收到了"像应答"的内容：说明这是自己（或另一台同主题设备）发出去的回包被投回来了，
        # 绝不能再回一次 err=unknown，否则两台设备会无限互刷。
        if line.startswith(REPLY_PREFIXES) or line == "pong":
            self._echo_dropped += 1
            if self._echo_dropped <= 3:
                self._log("warn", "忽略回投的应答（不是命令）：%s" % line[:40])
            return

        with self._mutex:
            # ---------------- 开锁 / 上锁 ----------------
            if line in ("unlock", "open", "on"):
                self.locked = False
                self._save()
                self.send("ack=unlock")
                self.send("state=unlock")
                self._log("info", "舵机转动：门锁已打开")
                self._on_change()
                # 固件里舵机转完会等 2 秒再自动回位（task_servo），这里必须一致，
                # 否则小程序提示"2 秒后自动闭锁"，而设备永远停在开锁状态。
                self._schedule_auto_lock()
                return

            if line in ("close", "lock", "off"):
                self._cancel_auto_lock()
                self.locked = True
                self._save()
                self.send("ack=close")
                self.send("state=lock")
                self._log("info", "舵机回位：门锁已上锁")
                self._on_change()
                return

            # ---------------- 状态 / 数量 / 探测 ----------------
            if line == "state":
                self.report_state()
                return

            if line in ("fp_num", "card_num"):
                self.report_counts()
                return

            if line == "ping":
                self.send("pong")
                return

            if line == "help":
                self.send("help=" + CMD_LIST)
                return

            # ---------------- 指纹：录入 ----------------
            if line in ("fp_add", "fp_enroll"):
                self.send("ack=fp_add")
                gen = self._begin_busy("指纹录入")
                if gen is not None:
                    threading.Thread(target=self._enroll_fp, args=(gen,), daemon=True).start()
                return

            if line == "fp_del":
                self.fps = []
                self._save()
                self.send("ack=fp_del")
                self._log("info", "指纹库已清空")
                self._on_change()
                return

            # ---------------- 指纹：列表 ----------------
            if line == "fp_list":
                if self.busy:
                    self.send("err=busy")
                    return
                ids = sorted(self.fps)
                for i in ids:
                    self.send("fp=%u" % i)
                self.send("fpend=%u" % len(ids))
                return

            # ---------------- 指纹：删除指定 ----------------
            if line.startswith("fp_del:"):
                rest = line[len("fp_del:"):]
                fid = _is_uint(rest)
                if fid is None or fid > 99999:
                    self.send("ack=fp_del_bad")
                    return
                if self.busy:
                    self.send("err=busy")
                    return
                if fid in self.fps:
                    self.fps.remove(fid)
                    self._save()
                    self._log("info", "已删除指纹 #%d" % fid)
                    self.send("ack=fp_del_ok")
                    self._on_change()
                else:
                    self.send("ack=fp_del_fail")
                return

            # ---------------- 卡：添加 ----------------
            if line == "card_add":
                self.send("ack=card_add")
                gen = self._begin_busy("添加 RFID 卡")
                if gen is not None:
                    threading.Thread(target=self._enroll_card, args=(gen,), daemon=True).start()
                return

            if line == "card_del":
                self.cards = []
                self._save()
                self.send("ack=card_del")
                self._log("info", "卡列表已清空")
                self._on_change()
                return

            if line == "card_list":
                for i, c in enumerate(self.cards[:CARD_MAX]):
                    self.send("card=%u,%s" % (i, _uid_to_text(c)))
                self.send("cardend=%u" % len(self.cards))
                return

            if line.startswith("card_del:"):
                rest = line[len("card_del:"):]
                idx = _is_uint(rest, 255)
                if idx is None:
                    self.send("ack=card_del_bad")
                    return
                if 0 <= idx < len(self.cards):
                    gone = self.cards.pop(idx)
                    self._save()
                    self._log("info", "已删除卡 #%d（%s），后续卡号前移" % (idx, _uid_to_text(gone)))
                    self.send("ack=card_del_ok")
                    self._on_change()
                else:
                    self.send("ack=card_del_fail")
                return

            # ---------------- 密码 ----------------
            if line.startswith("pwd:"):
                pwd = line[len("pwd:"):].strip()
                if pwd.isdigit() and 1 <= len(pwd) <= PASSWORD_MAX_LEN and pwd == self.password:
                    self.send("ack=pwd_ok")
                    self.locked = False
                    self._save()
                    self.send("state=unlock")
                    self._log("info", "密码正确，门锁已打开")
                    self._on_change()
                    # 密码开锁在固件里是"等同按了一次键盘"，最后一样进舵机队列，
                    # 所以同样要 2 秒后自动回锁（漏了这句就会出现"显示已开锁却一直不关"）
                    self._schedule_auto_lock()
                else:
                    self.send("ack=pwd_fail")
                    self._log("info", "密码错误：%s" % ("*" * len(pwd)))
                return

            if line.startswith("pwd_set:"):
                pwd = line[len("pwd_set:"):].strip()
                if pwd.isdigit() and 1 <= len(pwd) <= PASSWORD_MAX_LEN:
                    self.password = pwd
                    self._save()
                    self.send("ack=pwd_set_ok")
                    self._log("info", "开锁密码已修改")
                    self._on_change()
                else:
                    self.send("ack=pwd_set_bad")
                return

            self.send("err=unknown")
            self.send("help=" + CMD_LIST)

    # ------------------------------------------------------------ 模拟录入

    def _enroll_fp(self, gen):
        time.sleep(ENROLL_SECONDS)
        with self._mutex:
            new_id = next((i for i in range(FP_CAPACITY) if i not in self.fps), None)
            if new_id is not None:
                self.fps.append(new_id)
                self._save()
        self._end_busy(gen)
        if new_id is None:
            self._log("warn", "指纹库已满，录入失败")
            return
        self._log("info", "指纹录入成功：分配 ID #%d" % new_id)
        self.send("ack=fp_enrolled")
        self.report_counts()
        self._on_change()

    def _enroll_card(self, gen):
        time.sleep(ENROLL_SECONDS)
        with self._mutex:
            uid = bytes([random.randrange(256) for _ in range(4)])
            ok = len(self.cards) < CARD_MAX
            if ok:
                self.cards.append(uid)
                self._save()
        self._end_busy(gen)
        if not ok:
            self._log("warn", "卡列表已满（%d 张），添加失败" % CARD_MAX)
            return
        self._log("info", "刷卡成功：%s" % _uid_to_text(uid))
        self.send("ack=card_enrolled")
        self.report_counts()
        self._on_change()
