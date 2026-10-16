const store = require("../../utils/store.js");

function ch() {
  return getApp().globalData.channel;
}

Page({
  data: {
    cfg: null,
    // 表单用平铺字段，避免 WXML 里写多层路径
    url: "",
    clientId: "",
    topic: "",
    username: "",
    password: "",
    keepalive: 60,
    nameFilter: "",
    serviceId: "",
    characteristicId: "",
    uuidList: [],
    // 设备端（固件）使用的配置，仅供对照，改这里没用
    fwUid: "28eccdb351e4bf84ad1390c143846844",
    fwTopic: "lock001"
  },

  onLoad() {
    this.loadIntoForm();
  },

  loadIntoForm() {
    const c = store.load();
    this.setData({
      cfg: c,
      url: c.bemfa.url,
      clientId: c.bemfa.clientId,
      topic: c.bemfa.topic,
      username: c.bemfa.username,
      password: c.bemfa.password,
      keepalive: c.bemfa.keepalive,
      nameFilter: c.ble.nameFilter,
      serviceId: c.ble.serviceId,
      characteristicId: c.ble.characteristicId
    });
  },

  onInput(e) {
    const key = e.currentTarget.dataset.key;
    const patch = {};
    patch[key] = e.detail.value;
    this.setData(patch);
  },

  onSave() {
    const c = store.load();
    c.bemfa.url = (this.data.url || "").trim();
    c.bemfa.clientId = (this.data.clientId || "").trim() || "wxmp_lock01";
    c.bemfa.topic = (this.data.topic || "").trim();
    c.bemfa.username = (this.data.username || "").trim();
    c.bemfa.password = (this.data.password || "").trim();
    c.bemfa.keepalive = parseInt(this.data.keepalive, 10) || 60;
    c.ble.nameFilter = (this.data.nameFilter || "").trim();
    c.ble.serviceId = (this.data.serviceId || "").trim();
    c.ble.characteristicId = (this.data.characteristicId || "").trim();

    store.save(c);
    ch().reloadConfig();
    wx.showToast({ title: "已保存", icon: "success" });
    this.setData({ cfg: c });
  },

  onReset() {
    const self = this;
    wx.showModal({
      title: "恢复默认",
      content: "会清空本机保存的全部配置（含巴法云密钥）。",
      success: function (res) {
        if (!res.confirm) return;
        store.reset();
        ch().reloadConfig();
        self.loadIntoForm();
        wx.showToast({ title: "已恢复默认", icon: "none" });
      }
    });
  },

  /* ------------------------------------------------------------ 蓝牙排查 */

  onShowUuids() {
    const self = this;
    const c = ch();

    if (!c.ble.deviceId || !c.ble.connected) {
      wx.showToast({ title: "请先连接一个蓝牙设备", icon: "none" });
      return;
    }

    this.setData({ uuidList: [] });
    c.ble
      .scanServices()
      .then(function (list) {
        const rows = list.map(function (x) {
          return {
            serviceId: x.serviceId,
            characteristicId: x.characteristicId,
            properties: (x.properties || []).join(" / ")
          };
        });
        self.setData({ uuidList: rows });
      })
      .catch(function (e) {
        wx.showToast({ title: e.message || "读取失败", icon: "none" });
      });
  },

  onUseUuid(e) {
    const idx = e.currentTarget.dataset.index;
    const row = this.data.uuidList[idx];
    if (!row) return;
    this.setData({
      serviceId: row.serviceId,
      characteristicId: row.characteristicId
    });
    wx.showToast({ title: "已填入，记得保存", icon: "none" });
  },

  onTestCloud() {
    const c = ch();
    if (c.mode !== "cloud") {
      c.switchMode("cloud");
    }
    this.onSave();
    c.connectCloud();
    wx.showToast({ title: "正在连接，去控制页看日志", icon: "none" });
  },

  onGotoControl() {
    wx.switchTab({ url: "/pages/index/index" });
  }
});
