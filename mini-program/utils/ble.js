/**
 * utils/ble.js
 * 蓝牙 BLE 直连封装（微信小程序只支持 BLE 低功耗蓝牙，连不上 HC-05 这类经典蓝牙 SPP）
 *
 * 由于不同串口透传模块的 GATT 服务/特征值 UUID 千差万别，这里做了两件事：
 *   1. 自动识别：连上后遍历所有服务，挑出第一个可写特征值（写命令）
 *      和第一个支持 notify/indicate 的特征值（收应答）；
 *   2. 手动指定：设置页里填了 UUID 就优先用手填的。
 * 另外提供 scanServices()，把设备支持的全部 UUID 打印出来，
 * 方便你把自己的模块 UUID 抄到设置页里。
 */

const utf8 = require("./mqtt.js");

const WRITE_CHUNK = 20; // BLE 默认 ATT MTU 下的单包有效载荷，超过要分包

function BleChannel() {
  this.deviceId = null;
  this.serviceId = null;
  this.writeCharId = null;
  this.notifyCharId = null;
  this.connected = false;
  this.online = false;
  this.adapterReady = false;
  this.discovering = false;
  this.devices = []; // 扫描结果
  this.rx = ""; // 接收文本累积
  this.listeners = {};
  this.cfg = null;
}

BleChannel.prototype.on = function (evt, cb) {
  (this.listeners[evt] = this.listeners[evt] || []).push(cb);
  return this;
};

BleChannel.prototype.emit = function (evt, arg) {
  const ls = this.listeners[evt] || [];
  for (let i = 0; i < ls.length; i++) {
    try {
      ls[i](arg);
    } catch (e) {
      console.error("[ble] listener error", evt, e);
    }
  }
};

/* ------------------------------------------------------------------ 适配器 */

BleChannel.prototype.openAdapter = function () {
  const self = this;
  return new Promise(function (resolve, reject) {
    wx.openBluetoothAdapter({
      success: function () {
        self.adapterReady = true;
        self._bindEvents();
        resolve(true);
      },
      fail: function (err) {
        const msg =
          err && err.errCode === 10001
            ? "请打开手机蓝牙后重试"
            : "蓝牙初始化失败：" + JSON.stringify(err);
        self.emit("error", msg);
        reject(new Error(msg));
      }
    });
  });
};

BleChannel.prototype._bindEvents = function () {
  const self = this;

  wx.onBluetoothDeviceFound(function (res) {
    const list = (res && res.devices) || [];
    list.forEach(function (d) {
      const name = d.name || d.localName || "";
      if (!self.devices.some((x) => x.deviceId === d.deviceId)) {
        self.devices.push({
          deviceId: d.deviceId,
          name: name,
          rssi: d.RSSI != null ? d.RSSI : d.rssi
        });
        self.emit("device", self.devices[self.devices.length - 1]);
      }
    });
  });

  wx.onBLEConnectionStateChange(function (res) {
    if (res.deviceId === self.deviceId && !res.connected) {
      self.connected = false;
      self.online = false;
      self.emit("log", "蓝牙连接已断开");
      self.emit("closed", null);
    }
  });

  wx.onBLECharacteristicValueChange(function (res) {
    const text = utf8.utf8Decode(new Uint8Array(res.value));
    self.rx += text;
    // 设备端每条应答都以 \r\n 结尾
    let idx;
    while ((idx = self.rx.indexOf("\n")) >= 0) {
      const line = self.rx.slice(0, idx).replace(/\r/g, "");
      self.rx = self.rx.slice(idx + 1);
      if (line.length) self.emit("message", { topic: "(ble)", msg: line });
    }
    if (self.rx.length > 512) self.rx = ""; // 防御：异常数据不要无限增长
  });
};

/* ------------------------------------------------------------------ 扫描 */

BleChannel.prototype.startScan = function (nameFilter) {
  const self = this;
  this.devices = [];
  return new Promise(function (resolve, reject) {
    wx.startBluetoothDevicesDiscovery({
      allowDuplicatesKey: false,
      success: function () {
        self.discovering = true;
        if (nameFilter) {
          // 小程序不支持服务端过滤名称，这里只记录一下，由页面自行过滤
          self.nameFilter = nameFilter;
        }
        resolve(true);
      },
      fail: function (err) {
        reject(new Error("开始扫描失败：" + JSON.stringify(err)));
      }
    });
  });
};

BleChannel.prototype.stopScan = function () {
  const self = this;
  return new Promise(function (resolve) {
    wx.stopBluetoothDevicesDiscovery({
      complete: function () {
        self.discovering = false;
        resolve(true);
      }
    });
  });
};

/* ------------------------------------------------------------------ 连接 */

BleChannel.prototype.connect = function (deviceId, cfg) {
  const self = this;
  this.cfg = cfg || {};
  this.deviceId = deviceId;

  return new Promise(function (resolve, reject) {
    wx.createBLEConnection({
      deviceId: deviceId,
      timeout: 10000,
      success: function () {
        self.connected = true;
        self.emit("log", "已建立蓝牙连接，正在识别特征值…");
        self
          ._resolveCharacteristic()
          .then(function (info) {
            self.online = true;
            self.emit("log", "使用服务 " + info.serviceId);
            self.emit("log", "写入特征值 " + info.writeCharId);
            if (info.notifyCharId) self.emit("log", "通知特征值 " + info.notifyCharId);
            else self.emit("log", "该模块没有 notify 特征值，将收不到设备应答");
            self.emit("online", info);
            resolve(info);
          })
          .catch(reject);
      },
      fail: function (err) {
        self.emit("error", "蓝牙连接失败：" + JSON.stringify(err));
        reject(new Error("蓝牙连接失败"));
      }
    });
  });
};

function getServices(deviceId) {
  return new Promise(function (resolve, reject) {
    wx.getBLEDeviceServices({
      deviceId: deviceId,
      success: (res) => resolve(res.services || []),
      fail: (err) => reject(new Error("读取服务列表失败 " + JSON.stringify(err)))
    });
  });
}

function getChars(deviceId, serviceId) {
  return new Promise(function (resolve, reject) {
    wx.getBLEDeviceCharacteristics({
      deviceId: deviceId,
      serviceId: serviceId,
      success: (res) => resolve(res.characteristics || []),
      fail: (err) => reject(new Error("读取特征值失败 " + JSON.stringify(err)))
    });
  });
}

/** 设置页里可能填的是 16 位短 UUID，统一成完整长度再比较 */
function uuidEq(a, b) {
  const norm = (s) =>
    String(s || "")
      .toLowerCase()
      .replace(/^0000/, "")
      .replace(/-0000-1000-8000-00805f9b34fb$/, "");
  return norm(a) === norm(b);
}

/** 取 UUID 的 16 位短名（完整 128 位 UUID 的第 5~8 位；已经是短的就原样返回） */
function shortUuid(u) {
  const s = String(u || "").toLowerCase();
  return s.length === 32 ? s.slice(4, 8) : s;
}

BleChannel.prototype._resolveCharacteristic = function () {
  const self = this;
  const cfg = this.cfg || {};

  return getServices(this.deviceId).then(function (services) {
    // 打印全部服务/特征值，方便用户抄 UUID
    self.emit(
      "log",
      "服务列表: " + services.map((s) => s.uuid.toLowerCase()).join(", ")
    );

    // 用户手填了就直接用
    if (cfg.serviceId && cfg.characteristicId) {
      const svc = services.find((s) => uuidEq(s.uuid, cfg.serviceId)) || {
        uuid: cfg.serviceId
      };
      self.serviceId = svc.uuid;
      self.writeCharId = cfg.characteristicId;
      self.notifyCharId = cfg.characteristicId;
      return self._enableNotify().then(function () {
        return { serviceId: self.serviceId, writeCharId: self.writeCharId, notifyCharId: self.notifyCharId };
      });
    }

    // 自动识别：逐个服务找可写 / 可通知的特征值
    const IGNORE = ["1800", "1801", "180a"]; // 通用访问 / 属性服务，没有业务特征值
    let chain = Promise.resolve(null);
    let found = null;

    services.forEach(function (svc) {
      if (IGNORE.indexOf(shortUuid(svc.uuid)) >= 0) return;
      if (svc.isPrimary === false) return;

      chain = chain.then(function () {
        if (found) return null;
        return getChars(self.deviceId, svc.uuid).then(function (chars) {
          const names = chars.map(
            (c) =>
              c.uuid.toLowerCase() +
              "[" +
              Object.keys(c.properties || {})
                .filter((k) => c.properties[k])
                .join("|") +
              "]"
          );
          self.emit("log", "  " + svc.uuid.toLowerCase() + " -> " + names.join(" "));

          const w = chars.find(
            (c) => (c.properties && c.properties.write) || (c.properties && c.properties.writeNoResponse)
          );
          const n = chars.find(
            (c) => c.properties && (c.properties.notify || c.properties.indicate)
          );
          if (w) {
            found = { serviceId: svc.uuid, writeCharId: w.uuid, notifyCharId: n ? n.uuid : w.uuid };
          }
          return null;
        });
      });
    });

    return chain.then(function () {
      if (!found) {
        throw new Error(
          "没能自动识别可写特征值。请把控制台里打印的服务/特征值 UUID 填到设置页，或确认模块是 BLE 而非经典蓝牙。"
        );
      }
      self.serviceId = found.serviceId;
      self.writeCharId = found.writeCharId;
      self.notifyCharId = found.notifyCharId;
      return self._enableNotify().then(function () {
        return found;
      });
    });
  });
};

BleChannel.prototype._enableNotify = function () {
  const self = this;
  if (!this.notifyCharId) return Promise.resolve(true);

  return new Promise(function (resolve) {
    wx.notifyBLECharacteristicValueChange({
      deviceId: self.deviceId,
      serviceId: self.serviceId,
      characteristicId: self.notifyCharId,
      state: true,
      success: function () {
        resolve(true);
      },
      fail: function (err) {
        self.emit("log", "开启通知失败（不影响发送命令）：" + JSON.stringify(err));
        resolve(false);
      }
    });
  });
};

/** 扫描完把设备支持的全部 UUID 打出来，方便抄到设置页 */
BleChannel.prototype.scanServices = function () {
  const self = this;
  if (!this.deviceId) return Promise.resolve([]);
  const out = [];

  return getServices(this.deviceId).then(function (services) {
    let chain = Promise.resolve();
    services.forEach(function (svc) {
      chain = chain.then(function () {
        return getChars(self.deviceId, svc.uuid).then(function (chars) {
          chars.forEach(function (c) {
            const props = Object.keys(c.properties || {}).filter((k) => c.properties[k]);
            out.push({ serviceId: svc.uuid, characteristicId: c.uuid, properties: props });
          });
        });
      });
    });
    return chain.then(function () {
      return out;
    });
  });
};

/* ------------------------------------------------------------------ 收发 */

function strToArrayBuffer(s) {
  const bytes = utf8.utf8Encode(s);
  const buf = new ArrayBuffer(bytes.length);
  const view = new Uint8Array(buf);
  for (let i = 0; i < bytes.length; i++) view[i] = bytes[i];
  return buf;
}

BleChannel.prototype.send = function (line) {
  const self = this;
  if (!this.online || !this.writeCharId) return false;

  const buf = strToArrayBuffer(line + "\r\n");
  const bytes = new Uint8Array(buf);

  // 按 20 字节分包，串行发送
  let chain = Promise.resolve();
  for (let off = 0; off < bytes.length; off += WRITE_CHUNK) {
    const part = bytes.slice(off, off + WRITE_CHUNK);
    const ab = new ArrayBuffer(part.length);
    new Uint8Array(ab).set(part);
    chain = chain.then(function () {
      return new Promise(function (resolve) {
        wx.writeBLECharacteristicValue({
          deviceId: self.deviceId,
          serviceId: self.serviceId,
          characteristicId: self.writeCharId,
          value: ab,
          success: function () {
            resolve(true);
          },
          fail: function (err) {
            self.emit("error", "写入失败：" + JSON.stringify(err));
            resolve(false);
          }
        });
      });
    });
  }
  return true;
};

BleChannel.prototype.close = function () {
  const self = this;
  if (this.discovering) this.stopScan();
  if (this.deviceId) {
    wx.closeBLEConnection({ deviceId: this.deviceId, complete: function () {} });
  }
  this.connected = false;
  this.online = false;
  this.emit("closed", null);
};

module.exports = { BleChannel: BleChannel };
