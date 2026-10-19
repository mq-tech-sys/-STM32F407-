const theme = require("../../utils/theme.js");

function ch() {
  return getApp().globalData.channel;
}

function now() {
  const d = new Date();
  const p = (n) => (n < 10 ? "0" + n : "" + n);
  return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
}

Page({
  data: {
    theme: "light",
    mode: "cloud",
    online: false,
    password: "",
    newPassword: "",
    answers: [],
    fpCount: "-",
    cardCount: "-",
    fpList: [],
    cardList: [],
    fpLoading: false,
    cardLoading: false
  },

  onLoad() {
    const self = this;
    const c = ch();

    // 列表是"逐行下发 + 结束行"的，先攒在页面实例上，收完再一次性 setData
    this._fpBuf = [];
    this._cardBuf = [];
    this._loadedOnce = false;

    this.setData({ mode: c.mode, online: c.online });

    this._hStatus = function (s) {
      self.setData({ mode: s.mode, online: s.online });
      // 连上后第一次自动把两份列表拉下来
      if (s.online && !self._loadedOnce) {
        self._loadedOnce = true;
        self.refreshBoth();
      }
    };
    this._hMsg = function (m) {
      const msg = m && m.msg;
      if (!msg) return;

      /* ---- 数量 ---- */
      if (msg.indexOf("num=") === 0) {
        const p = msg.slice(4).split(",");
        if (p[0] === "fp") self.setData({ fpCount: p[1] });
        else self.setData({ cardCount: p[1] });
        self.pushAnswer(msg);
        return;
      }

      /* ---- 指纹列表：一行一个 fp=<id>，fpend=<个数> 收尾 ---- */
      if (msg.indexOf("fp=") === 0) {
        self._fpBuf.push(msg.slice(3).trim());
        return;
      }
      if (msg.indexOf("fpend=") === 0) {
        self.finishFpList(parseInt(msg.slice(6), 10) || 0);
        return;
      }

      /* ---- 卡列表：card=<索引>,<UID>，cardend=<个数> 收尾 ---- */
      if (msg.indexOf("card=") === 0) {
        const p = msg.slice(5).split(",");
        if (p.length >= 2) {
          self._cardBuf.push({
            idx: parseInt(p[0], 10) || 0,
            uid: (p[1] || "").toUpperCase()
          });
        }
        return;
      }
      if (msg.indexOf("cardend=") === 0) {
        self.finishCardList(parseInt(msg.slice(8), 10) || 0);
        return;
      }

      /* ---- 单项删除结果：成功后自动刷新对应列表 ---- */
      if (msg === "ack=fp_del_ok" || msg === "ack=card_del_ok") {
        self.pushAnswer(msg);
        setTimeout(function () {
          if (msg === "ack=fp_del_ok") self.onFpList(true);
          else self.onCardList(true);
        }, 400);
        return;
      }
      /* 设备正忙（正在录入/刷卡）：立刻停掉 loading，别让人对着转圈等超时 */
      if (msg === "err=busy") {
        self.setData({ fpLoading: false, cardLoading: false });
        self.pushAnswer("设备正忙（正在录入或刷卡），稍后再试");
        return;
      }
      if (msg === "ack=fp_del_fail" || msg === "ack=fp_del_bad") {
        self.setData({ fpLoading: false });
      }
      if (msg === "ack=card_del_fail" || msg === "ack=card_del_bad") {
        self.setData({ cardLoading: false });
      }
      if (msg === "ack=fp_del" || msg === "ack=card_del") {
        self.pushAnswer(msg);
        setTimeout(function () {
          if (msg === "ack=fp_del") self.onFpList(true);
          else self.onCardList(true);
        }, 800);
        return;
      }

      self.pushAnswer(msg);
    };
    this._hErr = function (text) {
      self.pushAnswer("错误：" + text);
    };

    c.on("status", this._hStatus);
    c.on("message", this._hMsg);
    c.on("error", this._hErr);
  },

  onShow() {
    const c = ch();
    this.setData({ online: c.isOnline(), theme: theme.sync() });
    if (c.isOnline() && !this._loadedOnce) {
      this._loadedOnce = true;
      this.refreshBoth();
    }
  },

  onToggleTheme() {
    this.setData({ theme: theme.toggle() });
  },

  /* ------------------------------------------------------------ 基础 */

  cmd(line, label) {
    const c = ch();
    if (!c.isOnline()) {
      wx.showToast({ title: "还没连接", icon: "none" });
      return false;
    }
    c.send(line);
    this.pushAnswer("（我）" + (label || line));
    return true;
  },

  confirmThen(title, content, line, label) {
    const self = this;
    wx.showModal({
      title: title,
      content: content,
      confirmText: "确定执行",
      cancelText: "取消",
      success: function (res) {
        if (res.confirm) self.cmd(line, label);
      }
    });
  },

  pushAnswer(text) {
    const answers = this.data.answers.slice();
    answers.unshift({ time: now(), text: text });
    if (answers.length > 30) answers.length = 30;
    this.setData({ answers: answers });
  },

  onClearAnswers() {
    this.setData({ answers: [] });
  },

  /* ------------------------------------------------------------ 指纹 */

  onFpAdd() {
    if (
      this.cmd(
        "fp_add",
        "fp_add  → 设备进入录入流程，请在门锁上连续按两次同一手指"
      )
    )
      wx.showToast({ title: "请在设备上按手指", icon: "none", duration: 2500 });
  },

  onFpDel() {
    this.confirmThen("清空指纹库", "会删除设备上全部指纹记录，无法撤销。", "fp_del", "fp_del（清空全部指纹）");
  },

  onFpNum() {
    this.cmd("fp_num", "fp_num（查询指纹数量）");
  },

  /** quiet=true 时是程序内部刷新，不记一条"（我）"日志 */
  onFpList(quiet) {
    const self = this;
    if (!this.cmd("fp_list", "fp_list（读取指纹列表）")) return;

    this._fpBuf = [];
    this.setData({ fpList: [], fpLoading: true });

    clearTimeout(this._fpTimer);
    this._fpTimer = setTimeout(function () {
      if (!self.data.fpLoading) return;
      self.setData({ fpLoading: false });
      self.pushAnswer("没收到指纹列表：设备可能正忙（正在录入/刷卡）或不在线");
    }, 6000);
  },

  finishFpList(n) {
    clearTimeout(this._fpTimer);
    const list = (this._fpBuf || []).map(function (id) {
      return { id: id };
    });
    this.setData({ fpList: list, fpLoading: false, fpCount: String(n) });
    this.pushAnswer(
      "指纹列表：" +
        (list.length
          ? list
              .map(function (x) {
                return "#" + x.id;
              })
              .join(" ")
          : "（空）")
    );
  },

  onFpDeleteItem(e) {
    const self = this;
    const id = e.currentTarget.dataset.id;
    wx.showModal({
      title: "删除指纹 #" + id,
      content: "删除后无法恢复，需要重新录入同一根手指。",
      confirmText: "删除",
      confirmColor: "#dc2626",
      success: function (res) {
        if (!res.confirm) return;
        self.setData({ fpLoading: true });
        self.cmd("fp_del:" + id, "fp_del:" + id + "（删除指纹 #" + id + "）");
      }
    });
  },

  /* ------------------------------------------------------------ RFID 卡 */

  onCardAdd() {
    if (this.cmd("card_add", "card_add  → 设备进入加卡流程，请把卡贴上读卡器"))
      wx.showToast({ title: "请刷卡", icon: "none", duration: 2500 });
  },

  onCardDel() {
    this.confirmThen("清空卡列表", "会删除设备上全部已登记的卡，无法撤销。", "card_del", "card_del（清空全部卡）");
  },

  onCardNum() {
    this.cmd("card_num", "card_num（查询卡片数量）");
  },

  onCardList(quiet) {
    const self = this;
    if (!this.cmd("card_list", "card_list（读取卡列表）")) return;

    this._cardBuf = [];
    this.setData({ cardList: [], cardLoading: true });

    clearTimeout(this._cardTimer);
    this._cardTimer = setTimeout(function () {
      if (!self.data.cardLoading) return;
      self.setData({ cardLoading: false });
      self.pushAnswer("没收到卡列表：设备可能正忙（正在加卡/刷卡）或不在线");
    }, 4000);
  },

  finishCardList(n) {
    clearTimeout(this._cardTimer);
    const list = (this._cardBuf || []).slice().sort(function (a, b) {
      return a.idx - b.idx;
    });
    this.setData({ cardList: list, cardLoading: false, cardCount: String(n) });
    this.pushAnswer(
      "卡列表：" +
        (list.length
          ? list
              .map(function (x) {
                return "#" + x.idx + " " + x.uid;
              })
              .join(" / ")
          : "（空）")
    );
  },

  onCardDeleteItem(e) {
    const self = this;
    const idx = e.currentTarget.dataset.idx;
    wx.showModal({
      title: "删除卡 #" + idx,
      content: "删除后这张卡将无法再刷卡开锁。",
      confirmText: "删除",
      confirmColor: "#dc2626",
      success: function (res) {
        if (!res.confirm) return;
        self.setData({ cardLoading: true });
        self.cmd("card_del:" + idx, "card_del:" + idx + "（删除卡 #" + idx + "）");
      }
    });
  },

  /** 连上后把两份列表一起拉一遍 */
  refreshBoth() {
    if (!ch().isOnline()) return;
    this.onFpList(true);
    this.onCardList(true);
  },

  /* ------------------------------------------------------------ 密码 */

  onPwdInput(e) {
    this.setData({ password: e.detail.value });
  },

  onNewPwdInput(e) {
    this.setData({ newPassword: e.detail.value });
  },

  onPwdUnlock() {
    const p = (this.data.password || "").trim();
    if (!/^\d{1,6}$/.test(p)) {
      wx.showToast({ title: "密码只能是 1~6 位数字", icon: "none" });
      return;
    }
    // 不把明文写进日志，只记录"已发送"
    const c = ch();
    if (!c.isOnline()) {
      wx.showToast({ title: "还没连接", icon: "none" });
      return;
    }
    c.send("pwd:" + p);
    this.pushAnswer("（我）pwd:******");
    this.setData({ password: "" });
  },

  onPwdSet() {
    const p = (this.data.newPassword || "").trim();
    if (!/^\d{1,6}$/.test(p)) {
      wx.showToast({ title: "新密码只能是 1~6 位数字", icon: "none" });
      return;
    }
    const c = ch();
    if (!c.isOnline()) {
      wx.showToast({ title: "还没连接", icon: "none" });
      return;
    }
    c.send("pwd_set:" + p);
    this.pushAnswer("（我）pwd_set:******");
    this.setData({ newPassword: "" });
    wx.showToast({ title: "已发送修改密码", icon: "none" });
  },

  /* ------------------------------------------------------------ 其他 */

  onHelp() {
    this.cmd("help", "help（查看设备支持的命令）");
  },

  onPing() {
    this.cmd("ping", "ping（链路探测）");
  },

  onGotoControl() {
    wx.switchTab({ url: "/pages/index/index" });
  }
});
