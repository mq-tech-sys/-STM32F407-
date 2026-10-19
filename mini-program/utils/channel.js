/**
 * utils/channel.js
 * 统一通道抽象：把「巴法云 MQTT」和「蓝牙 BLE 直连」两条完全不同的链路
 * 收敛成同一组接口，页面代码不需要关心底层是哪个。
 *
 *   对外接口： on(evt, cb) / connect() / send(line) / close() / switchMode(mode)
 *   对外事件： status  连接/在线状态变化 {mode, connected, online}
 *              message 收到设备消息 {topic, msg}
 *              log     过程日志（给页面显示）
 *              error   错误
 *              devices 蓝牙扫描到的设备列表
 */

const store = require("./store.js");
const mqttMod = require("./mqtt.js");
const bleMod = require("./ble.js");

function Channel() {
  this.mode = "cloud";
  this.cfg = store.load();
  this.mqtt = new mqttMod.MqttClient();
  this.ble = new bleMod.BleChannel();
  this.listeners = {};
  this.status = { mode: "cloud", connected: false, online: false };
  this.reconnectTimer = null;

  this._wire();
}

Channel.prototype.on = function (evt, cb) {
  (this.listeners[evt] = this.listeners[evt] || []).push(cb);
  return this;
};

Channel.prototype.emit = function (evt, arg) {
  const ls = this.listeners[evt] || [];
  for (let i = 0; i < ls.length; i++) {
    try {
      ls[i](arg);
    } catch (e) {
      console.error("[channel] listener", evt, e);
    }
  }
};

Channel.prototype._status = function (patch) {
  this.status = Object.assign({}, this.status, patch);
  this.emit("status", this.status);
};

/** 把两个底层客户端的同名事件转发到本对象上 */
Channel.prototype._wire = function () {
  const self = this;

  ["log", "error", "message", "device"].forEach(function (evt) {
    self.mqtt.on(evt, function (a) {
      if (self.mode === "cloud") self.emit(evt, a);
    });
  });

  self.ble.on("log", function (a) {
    if (self.mode === "ble") self.emit("log", a);
  });
  self.ble.on("error", function (a) {
    if (self.mode === "ble") self.emit("error", a);
  });
  self.ble.on("message", function (a) {
    if (self.mode === "ble") self.emit("message", a);
  });
  self.ble.on("device", function (a) {
    if (self.mode === "ble") self.emit("device", a);
  });
  self.ble.on("scan", function (a) {
    if (self.mode === "ble") self.emit("scan", a);
  });

  self.mqtt.on("online", function () {
    self._status({ connected: true, online: true });
  });
  self.mqtt.on("pong", function () {
    self._status({ connected: true, online: true });
  });
  self.mqtt.on("closed", function () {
    self._status({ connected: false, online: false });
    if (self.mode === "cloud") self._scheduleReconnect();
  });

  self.ble.on("online", function () {
    self._status({ connected: true, online: true });
  });
  self.ble.on("closed", function () {
    self._status({ connected: false, online: false });
  });
};

Channel.prototype.switchMode = function (mode) {
  if (mode === this.mode) return;
  this.close();
  this.mode = mode;
  this.cfg.mode = mode;
  store.save(this.cfg);
  this._status({ mode: mode, connected: false, online: false });
  this.emit("log", "已切换到" + (mode === "cloud" ? "云端 MQTT" : "蓝牙直连") + "模式");
};

Channel.prototype.reloadConfig = function () {
  this.cfg = store.load();
};

/* --------------------------------------------------------------- 云端 MQTT */

Channel.prototype.connectCloud = function () {
  const self = this;
  this.reloadConfig();
  const b = this.cfg.bemfa;

  if (!b.topic) {
    this.emit("error", "还没填主题名，去「设置」页填一个");
    return;
  }

  this.emit("log", "云端模式：主题 " + b.topic + "，ClientID " + b.clientId);
  this._status({ mode: "cloud", connected: false, online: false });

  this.mqtt.connect({
    url: b.url,
    clientId: b.clientId,
    username: b.username,
    password: b.password,
    keepalive: b.keepalive,
    // 订阅设备所在的主题即可收到设备发布到 <topic>/set 的状态
    topics: [b.topic]
  });

  // 云端 5 秒后把本地记录的门锁状态问一遍
  this._askTimer = setTimeout(function () {
    if (self.mqtt.online) self.send("state");
  }, 5000);
};

/* ------------------------------------------------------------ 蓝牙 BLE */

Channel.prototype.openBle = function () {
  return this.ble.openAdapter();
};

Channel.prototype.scanBle = function () {
  this.reloadConfig();
  return this.ble.startScan(this.cfg.ble.nameFilter);
};

/** 蓝牙自检：扫描为空时先看这个结果 */
Channel.prototype.diagnoseBle = function () {
  return this.ble.diagnose();
};

Channel.prototype.connectBle = function (deviceId) {
  const self = this;
  this.reloadConfig();
  this._status({ mode: "ble", connected: false, online: false });

  return this.ble.connect(deviceId, this.cfg.ble).then(function (info) {
    self.cfg.ble.deviceId = deviceId;
    if (!self.cfg.ble.serviceId) self.cfg.ble.serviceId = info.serviceId;
    if (!self.cfg.ble.characteristicId) self.cfg.ble.characteristicId = info.writeCharId;
    store.save(self.cfg);
    self._status({ mode: "ble", connected: true, online: true });
    // 连上后先问一次状态
    setTimeout(function () {
      self.send("state");
    }, 600);
    return info;
  });
};

/* --------------------------------------------------------------- 统一接口 */

Channel.prototype.connect = function () {
  if (this.mode === "cloud") {
    this.connectCloud();
    return Promise.resolve(true);
  }
  return this.openBle().then(() => {
    const self = this;
    if (this.cfg.ble.deviceId) {
      this.emit("log", "尝试重连上次的蓝牙设备…");
      return this.connectBle(this.cfg.ble.deviceId).catch(function () {
        self.emit("log", "重连失败，请手动扫描选择设备");
        return false;
      });
    }
    return false;
  });
};

/**
 * 发一条命令给设备。
 * 云端模式走 <topic>/set，蓝牙模式直接往特征值写一行文本，两者文本格式完全一致。
 */
Channel.prototype.send = function (line) {
  if (this.mode === "cloud") {
    const topic = this.cfg.bemfa.topic + "/set";
    const ok = this.mqtt.publish(topic, line);
    this.emit("log", (ok ? "→ " : "× 未连接，丢弃 ") + line);
    return ok;
  }
  const ok = this.ble.send(line);
  this.emit("log", (ok ? "→ " : "× 未连接，丢弃 ") + line);
  return ok;
};

Channel.prototype.close = function () {
  if (this._askTimer) {
    clearTimeout(this._askTimer);
    this._askTimer = null;
  }
  this._cancelReconnect();
  this.mqtt.close();
  if (this.mode === "ble") this.ble.close();
  this._status({ connected: false, online: false });
};

Channel.prototype.isOnline = function () {
  return this.mode === "cloud" ? this.mqtt.online : this.ble.online;
};

/* --------------------------------------------------------------- 断线重连 */

/** ponytail: 固定 5 秒重连，不做指数退避 —— 门锁场景下够用且更好排查 */
Channel.prototype._scheduleReconnect = function () {
  const self = this;
  if (this.reconnectTimer || this.mode !== "cloud") return;
  this.emit("log", "5 秒后重连云端…");
  this.reconnectTimer = setTimeout(function () {
    self.reconnectTimer = null;
    if (self.mode === "cloud" && !self.mqtt.online) self.connectCloud();
  }, 5000);
};

Channel.prototype._cancelReconnect = function () {
  if (this.reconnectTimer) {
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
  }
};

module.exports = { Channel: Channel };
