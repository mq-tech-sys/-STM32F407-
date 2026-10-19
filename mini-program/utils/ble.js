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
  this.devices = []; // 扫描结果（全部设备，含不匹配过滤的）
  this.filter = ""; // 设备名关键字，只用于给设备打 matched 标记，不再隐藏
  this.rx = ""; // 接收文本累积
  this.listeners = {};
  this.cfg = null;
  this._bound = false;
  this._scanTimer = null;
  this._pullTimer = null;
  this.lastError = "";
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
  if (this.adapterReady) return Promise.resolve(true);

  return new Promise(function (resolve, reject) {
    wx.openBluetoothAdapter({
      success: function () {
        self.adapterReady = true;
        self._bindEvents();
        resolve(true);
      },
      fail: function (err) {
        const code = err && err.errCode;
        let msg = "蓝牙初始化失败：" + JSON.stringify(err);
        if (code === 10001) {
          msg =
            "手机蓝牙没打开。请到系统设置里打开蓝牙" +
            "（安卓还要把「定位」开关也打开，扫码必须靠它）";
        }
        self.lastError = msg;
        self.emit("error", msg);
        reject(new Error(msg));
      }
    });
  });
};

/** 扫描/诊断前确保适配器已初始化，避免直接调扫描报 10000「未初始化蓝牙适配器」 */
BleChannel.prototype.ensureAdapter = function () {
  return this.adapterReady ? Promise.resolve(true) : this.openAdapter();
};

BleChannel.prototype._bindEvents = function () {
  const self = this;
  if (this._bound) return; // 只注册一次，否则每次 openAdapter 都会重复挂监听
  this._bound = true;

  wx.onBluetoothDeviceFound(function (res) {
    self._mergeDevices((res && res.devices) || []);
  });

  wx.onBluetoothAdapterStateChange(function (res) {
    self.adapterReady = !!(res && res.available);
    self.emit("scan", self.scanStat());
    if (res && !res.available) {
      self.lastError = "系统蓝牙已关闭，扫描中断";
      self.emit("error", self.lastError);
      self._finishScan();
    }
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

/** 名字是否命中过滤词（不区分大小写）；过滤词为空时一律算命中 */
function nameMatches(name, filter) {
  const f = String(filter || "").trim().toLowerCase();
  if (!f) return true;
  return String(name || "").toLowerCase().indexOf(f) >= 0;
}

/** 把 onBluetoothDeviceFound / getBluetoothDevices 的结果合并进列表 */
BleChannel.prototype._mergeDevices = function (list) {
  const self = this;
  let changed = false;

  (list || []).forEach(function (d) {
    if (!d || !d.deviceId) return;
    const name = d.name || d.localName || "";
    const rssi = d.RSSI != null ? d.RSSI : d.rssi;
    const old = self.devices.filter((x) => x.deviceId === d.deviceId)[0];

    if (!old) {
      self.devices.push({
        deviceId: d.deviceId,
        name: name,
        rssi: rssi == null ? 0 : rssi,
        matched: nameMatches(name, self.filter)
      });
      changed = true;
    } else {
      // 名字后到（有些安卓机先报 deviceId、后补名字），补上
      if (!old.name && name) {
        old.name = name;
        changed = true;
      }
      if (rssi != null && rssi !== old.rssi) {
        old.rssi = rssi;
        changed = true;
      }
      const m = nameMatches(old.name, self.filter);
      if (m !== old.matched) {
        old.matched = m;
        changed = true;
      }
    }
  });

  if (changed) {
    // 信号强的排前面，找模块时不用翻
    this.devices.sort(function (a, b) {
      return (b.rssi || -999) - (a.rssi || -999);
    });
  }
  this.emit("scan", this.scanStat());
  return changed;
};

/** 当前扫描统计，给页面显示 */
BleChannel.prototype.scanStat = function () {
  const named = this.devices.filter((x) => !!x.name).length;
  const matched = this.devices.filter((x) => x.matched).length;
  return {
    discovering: this.discovering,
    total: this.devices.length,
    named: named,
    matched: matched,
    filter: this.filter,
    error: this.lastError || ""
  };
};

/**
 * 部分安卓机不会把设备推给 onBluetoothDeviceFound，但会出现在
 * getBluetoothDevices 里，所以扫描期间再定时拉一遍兜底。
 */
BleChannel.prototype._pullDevices = function () {
  const self = this;
  if (typeof wx.getBluetoothDevices !== "function") return;
  wx.getBluetoothDevices({
    success: function (res) {
      if (self.discovering) self._mergeDevices((res && res.devices) || []);
    },
    fail: function () {}
  });
};

BleChannel.prototype._finishScan = function () {
  if (this._scanTimer) {
    clearTimeout(this._scanTimer);
    this._scanTimer = null;
  }
  if (this._pullTimer) {
    clearInterval(this._pullTimer);
    this._pullTimer = null;
  }
  const was = this.discovering;
  this.discovering = false;
  if (was) {
    wx.stopBluetoothDevicesDiscovery({ complete: function () {} });
  }
  this.emit("scan", this.scanStat());
};

/**
 * 开始扫描。会先确保适配器已初始化（否则必然报 10000）。
 * 只显示"附近全部 BLE 设备"，不按名字过滤；命中过滤词的设备会带
 * matched=true，页面自己决定怎么标。
 */
BleChannel.prototype.startScan = function (nameFilter, durationMs) {
  const self = this;

  this.filter = String(nameFilter || "").trim();
  this.devices = [];
  this.lastError = "";

  return this.ensureAdapter()
    .then(function () {
      return new Promise(function (resolve, reject) {
        wx.getBluetoothAdapterState({
          success: function (st) {
            if (st && st.available === false) {
              return reject(
                new Error("系统蓝牙未开启（安卓请同时打开定位开关），打开后重试")
              );
            }
            resolve(true);
          },
          // 老版本基础库没有这个接口，直接当可用继续
          fail: function () {
            resolve(true);
          }
        });
      });
    })
    .then(function () {
      // 上一次没停干净时先停再开，避免 10008
      return new Promise(function (resolve) {
        wx.stopBluetoothDevicesDiscovery({
          complete: function () {
            resolve(true);
          }
        });
      });
    })
    .then(function () {
      return new Promise(function (resolve, reject) {
        wx.startBluetoothDevicesDiscovery({
          allowDuplicatesKey: false, // 去重在 _mergeDevices 里做，这里省事件量
          success: function () {
            self.discovering = true;
            self.emit("scan", self.scanStat());
            resolve(true);
          },
          fail: function (err) {
            const code = err && err.errCode;
            let msg = "开始扫描失败：" + JSON.stringify(err);
            if (code === 10000) msg = "蓝牙适配器未初始化，退出小程序重进一次再试";
            if (code === 10001) msg = "系统蓝牙未开启，请打开蓝牙（安卓还需打开定位）";
            if (code === 10009) msg = "当前系统版本不支持 BLE 扫描";
            if (code === 10008) msg = "系统蓝牙异常，关掉蓝牙再打开一次试试";
            self.lastError = msg;
            self.emit("error", msg);
            reject(new Error(msg));
          }
        });
      });
    })
    .then(function () {
      // 兜底轮询 + 自动收尾
      self._pullTimer = setInterval(function () {
        self._pullDevices();
      }, 2000);
      self._pullDevices();

      const ms = durationMs || 12000;
      self._scanTimer = setTimeout(function () {
        self._finishScan();
      }, ms);
      return true;
    });
};

BleChannel.prototype.stopScan = function () {
  this._finishScan();
  return Promise.resolve(true);
};

/**
 * 蓝牙自检：把适配器状态、系统开关、授权情况一次性取回来。
 * 扫描为空时先看这里，能区分「没权限 / 系统蓝牙关了 / 附近真没有设备」。
 */
BleChannel.prototype.diagnose = function () {
  const self = this;

  function safe(fn) {
    return new Promise(function (resolve) {
      if (typeof wx[fn] !== "function") return resolve(null);
      try {
        wx[fn]({
          success: function (res) {
            resolve(res || null);
          },
          fail: function () {
            resolve(null);
          }
        });
      } catch (e) {
        resolve(null);
      }
    });
  }

  return this.ensureAdapter()
    .catch(function () {
      return false;
    })
    .then(function () {
      return Promise.all([
        safe("getBluetoothAdapterState"),
        safe("getSystemSetting"),
        safe("getAppAuthorizeSetting"),
        safe("getSetting")
      ]);
    })
    .then(function (r) {
      const adapter = r[0] || {};
      const sys = r[1] || {};
      const auth = r[2] || {};
      const setting = (r[3] && r[3].authSetting) || {};

      return {
        adapterReady: self.adapterReady,
        available: adapter.available === true,
        discovering: adapter.discovering === true || self.discovering,
        bluetoothEnabled:
          typeof sys.bluetoothEnabled === "boolean" ? sys.bluetoothEnabled : null,
        locationEnabled:
          typeof sys.locationEnabled === "boolean" ? sys.locationEnabled : null,
        bluetoothAuthorized: auth.bluetoothAuthorized || null,
        locationAuthorized: auth.locationAuthorized || null,
        scopeBluetooth: setting["scope.bluetooth"] || null,
        scopeLocation: setting["scope.userLocation"] || null,
        found: self.devices.length,
        error: self.lastError || ""
      };
    });
};

/* ------------------------------------------------------------------ 连接 */

BleChannel.prototype.connect = function (deviceId, cfg) {
  const self = this;
  this.cfg = cfg || {};
  this.deviceId = deviceId;

  return this.ensureAdapter().then(function () {
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
          const code = err && err.errCode;
          let msg = "蓝牙连接失败：" + JSON.stringify(err);
          if (code === 10003) {
            msg = "连接失败：设备不在范围内，或已被其他手机连上（先断开再试）";
          } else if (code === 10012) {
            msg = "连接超时：靠近模块重试，或先给模块断电重启";
          } else if (code === 10002) {
            msg = "没有找到该设备，重新扫描一次再连";
          }
          self.emit("error", msg);
          reject(new Error(msg));
        }
      });
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
