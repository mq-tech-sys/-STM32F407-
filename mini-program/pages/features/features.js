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
    mode: "cloud",
    online: false,
    password: "",
    newPassword: "",
    answers: [],
    fpCount: "-",
    cardCount: "-"
  },

  onLoad() {
    const self = this;
    const c = ch();

    this.setData({ mode: c.mode, online: c.online });

    this._hStatus = function (s) {
      self.setData({ mode: s.mode, online: s.online });
    };
    this._hMsg = function (m) {
      const msg = m && m.msg;
      if (!msg) return;

      if (msg.indexOf("num=") === 0) {
        const p = msg.slice(4).split(",");
        if (p[0] === "fp") self.setData({ fpCount: p[1] });
        else self.setData({ cardCount: p[1] });
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
    this.setData({ online: c.isOnline() });
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
