const { Channel } = require("./utils/channel.js");

App({
  globalData: {
    channel: null
  },

  onLaunch() {
    // 全局只建一个通道实例，三个页面共用同一条连接
    this.globalData.channel = new Channel();
    const cfg = this.globalData.channel.cfg;
    this.globalData.channel.mode = cfg.mode || "cloud";
    this.globalData.channel._status({ mode: this.globalData.channel.mode });
  },

  onShow() {},

  onHide() {
    // 切到后台不断开：回来还能直接看到状态。
    // 如需省电可在这里调用 getApp().globalData.channel.close();
  }
});
