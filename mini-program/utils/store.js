/**
 * utils/store.js
 * 本地配置读写（wx.setStorageSync 持久化）
 */

const KEY = "smart_lock_cfg_v1";

const DEFAULTS = {
  mode: "cloud", // cloud = 巴法云 MQTT 远程 | ble = 蓝牙直连
  bemfa: {
    // 巴法云的微信小程序专用入口：加密 WebSocket，端口 9504，路径 /wss
    url: "wss://bemfa.com:9504/wss",
    // 设备端用的是「私钥」当 ClientID。小程序千万不能也用同一个，
    // 否则会把设备顶下线。这里用独立的固定 ID。
    clientId: "wxmp_lock01",
    // 要订阅/发布的主题（与固件里 BAFA_TOPIC_LOCK 一致）
    topic: "lock001",
    // 若 CONNACK 返回 4/5，说明需要用户名密码认证，去巴法云「连接密钥」页拿
    username: "",
    password: "",
    keepalive: 60
  },
  ble: {
    nameFilter: "", // 设备名关键字，留空则显示全部
    serviceId: "", // 留空 = 连上后自动识别
    characteristicId: "",
    deviceId: "" // 上次连过的设备，方便一键重连
  }
};

function deepMerge(base, over) {
  const out = {};
  Object.keys(base).forEach(function (k) {
    out[k] = base[k];
  });
  if (!over) return out;
  Object.keys(over).forEach(function (k) {
    const bv = base[k];
    const ov = over[k];
    if (bv && typeof bv === "object" && !Array.isArray(bv)) {
      out[k] = deepMerge(bv, ov);
    } else if (ov !== undefined && ov !== null) {
      out[k] = ov;
    }
  });
  return out;
}

function load() {
  let saved = null;
  try {
    saved = wx.getStorageSync(KEY);
  } catch (e) {
    saved = null;
  }
  return deepMerge(DEFAULTS, saved);
}

function save(cfg) {
  try {
    wx.setStorageSync(KEY, cfg);
  } catch (e) {
    console.error("[store] 保存失败", e);
  }
}

function reset() {
  try {
    wx.removeStorageSync(KEY);
  } catch (e) {}
  return load();
}

module.exports = { KEY, DEFAULTS, load, save, reset };
