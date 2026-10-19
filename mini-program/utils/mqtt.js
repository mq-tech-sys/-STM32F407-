/**
 * utils/mqtt.js
 * 微信小程序用的 MQTT 3.1.1 客户端（跑在 wx.connectSocket 之上）
 *
 * 为什么必须自己实现：
 *   1. 小程序没有原生 TCP，只能走 WebSocket；
 *   2. MQTT over WebSocket 规定用「二进制帧」承载 MQTT 报文，所以不能用
 *      巴法云那种 cmd=1&uid=.. 的文本协议；
 *   3. 小程序环境里不能直接引 mqtt.js（体积/兼容性都麻烦），这里只实现
 *      实际用得到的 5 种报文：CONNECT / CONNACK / SUBSCRIBE / PUBLISH / PINGREQ。
 *
 * 与设备端 HARDWARE/net/mqtt_packet.c 是同一套协议的两个实现，行为对齐。
 */

const T = {
  CONNECT: 1,
  CONNACK: 2,
  PUBLISH: 3,
  PUBACK: 4,
  SUBSCRIBE: 8,
  SUBACK: 9,
  PINGREQ: 12,
  PINGRESP: 13,
  DISCONNECT: 14
};

/* ------------------------------------------------------------------ 编码 */

/** 可变长"剩余长度"字段 */
function encodeLength(n) {
  const out = [];
  do {
    let b = n % 128;
    n = Math.floor(n / 128);
    if (n > 0) b |= 0x80;
    out.push(b);
  } while (n > 0);
  return out;
}

function encodeString(str) {
  const utf8 = utf8Encode(str);
  const out = [utf8.length >> 8, utf8.length & 0xff];
  for (let i = 0; i < utf8.length; i++) out.push(utf8[i]);
  return out;
}

/** ASCII 走快路径，其余按标准 UTF-8 编码 */
function utf8Encode(str) {
  const out = [];
  for (let i = 0; i < str.length; i++) {
    let c = str.charCodeAt(i);
    if (c < 0x80) {
      out.push(c);
    } else if (c < 0x800) {
      out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
    } else if (c >= 0xd800 && c <= 0xdbff) {
      // 代理对
      const c2 = str.charCodeAt(++i);
      const cp = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
      out.push(
        0xf0 | (cp >> 18),
        0x80 | ((cp >> 12) & 0x3f),
        0x80 | ((cp >> 6) & 0x3f),
        0x80 | (cp & 0x3f)
      );
    } else {
      out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
    }
  }
  return out;
}

function utf8Decode(bytes) {
  let out = "";
  for (let i = 0; i < bytes.length; ) {
    const b = bytes[i];
    if (b < 0x80) {
      out += String.fromCharCode(b);
      i += 1;
    } else if (b < 0xe0) {
      out += String.fromCharCode(((b & 0x1f) << 6) | (bytes[i + 1] & 0x3f));
      i += 2;
    } else if (b < 0xf0) {
      out += String.fromCharCode(
        ((b & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f)
      );
      i += 3;
    } else {
      const cp =
        ((b & 0x07) << 18) |
        ((bytes[i + 1] & 0x3f) << 12) |
        ((bytes[i + 2] & 0x3f) << 6) |
        (bytes[i + 3] & 0x3f);
      const c = cp - 0x10000;
      out += String.fromCharCode(0xd800 + (c >> 10), 0xdc00 + (c & 0x3ff));
      i += 4;
    }
  }
  return out;
}

function toArrayBuffer(bytes) {
  const buf = new ArrayBuffer(bytes.length);
  const view = new Uint8Array(buf);
  for (let i = 0; i < bytes.length; i++) view[i] = bytes[i];
  return buf;
}

function packet(typeFlags, body) {
  return toArrayBuffer([typeFlags].concat(encodeLength(body.length), body));
}

/* ------------------------------------------------------------------ 客户端 */

function MqttClient() {
  this.socket = null;
  this.connected = false; // WebSocket 已开
  this.online = false; // MQTT 已握手 + 已订阅
  this.buffer = []; // 收到但尚未组包完的字节
  this.listeners = {};
  this.msgId = 1;
  this.timer = null;
  this.keepalive = 60;
  this.manualClose = false;
  this.cfg = null;
  this.lastRx = 0; // 最后一次收到报文的时刻，用来发现「假在线」
  this._connectTimer = null;
  this._failed = false;
}

MqttClient.prototype.on = function (evt, cb) {
  (this.listeners[evt] = this.listeners[evt] || []).push(cb);
  return this;
};

MqttClient.prototype.emit = function (evt, arg) {
  const ls = this.listeners[evt] || [];
  for (let i = 0; i < ls.length; i++) {
    try {
      ls[i](arg);
    } catch (e) {
      console.error("[mqtt] listener error", evt, e);
    }
  }
};

/**
 * @param cfg {url, clientId, username, password, keepalive, topics}
 */
MqttClient.prototype.connect = function (cfg) {
  const self = this;
  this.cfg = cfg;
  this.keepalive = cfg.keepalive || 60;
  this.manualClose = false;

  // 每次建链先把上一轮状态清干净。
  // connected / online 是「上次连上过」的残留真值：旧 socket 悄悄断掉后若不清，
  // 界面仍然允许发命令（日志只有 → 没有 ←），表现就是「连接好久、怎么控制都没反应」。
  this.connected = false;
  this.online = false;
  this.buffer = [];
  this.lastRx = 0;
  this._failed = false;
  this._stopTimer();
  this._clearConnectTimer();
  this._closeSocket();

  this.emit("log", "正在连接 " + cfg.url);

  const task = wx.connectSocket({
    url: cfg.url,
    success: () => {},
    fail: function (err) {
      self._fail("WebSocket 建立失败：" + JSON.stringify(err));
    }
  });
  this.socket = task;

  // wx.onSocketXxx 是全局注册的：反复重连会叠加多份回调，旧 socket 的事件也可能串进来，
  // 表现就是「看起来连上了，却一条报文都收不到」。优先用 SocketTask 自己的事件，
  // 事件与这根 socket 一一对应。
  const bind = function (name, globalName, cb) {
    if (task && typeof task[name] === "function") {
      task[name](cb);
    } else if (wx && typeof wx[globalName] === "function") {
      wx[globalName](cb);
    }
  };

  bind("onOpen", "onSocketOpen", function () {
    self.connected = true;
    self.lastRx = Date.now();
    self.emit("log", "WebSocket 已打开，发送 MQTT CONNECT");
    const body = [];
    // 可变头
    body.push(0x00, 0x04, 0x4d, 0x51, 0x54, 0x54, 0x04); // "MQTT", level 4
    let flags = 0x02; // clean session
    if (cfg.username) flags |= 0x80;
    if (cfg.password) flags |= 0xc0;
    body.push(flags, self.keepalive >> 8, self.keepalive & 0xff);
    // 载荷
    const cid = encodeString(cfg.clientId || "wxmp_lock");
    const user = cfg.username ? encodeString(cfg.username) : [];
    const pass = cfg.password ? encodeString(cfg.password) : [];
    const all = body.concat(cid, user, pass);
    self.socket.send({
      data: packet(T.CONNECT << 4, all),
      fail: function (e) {
        self.emit("error", "CONNECT 发送失败 " + JSON.stringify(e));
      }
    });
  });

  bind("onMessage", "onSocketMessage", function (res) {
    self._feed(res && res.data);
  });

  bind("onError", "onSocketError", function (err) {
    self.emit("error", "WebSocket 错误：" + JSON.stringify(err));
  });

  bind("onClose", "onSocketClose", function (res) {
    self._clearConnectTimer();
    self.connected = false;
    self.online = false;
    self._stopTimer();
    self.emit("log", "连接已关闭 code=" + ((res && res.code) || "?"));
    self.emit("closed", res);
  });

  // 看门狗：卡在建链或卡在握手时给一句明确结论，而不是永远停在「正在连接」
  this._connectTimer = setTimeout(function () {
    self._connectTimer = null;
    if (self.manualClose) return;
    if (!self.connected) {
      self._fail("8 秒没能建立 WebSocket（域名白名单 / 网络 / 端口被拦）");
    } else if (!self.online) {
      self._fail("WebSocket 已连上，但 8 秒内没收到 CONNACK 或订阅应答");
    }
  }, 8000);
  if (this._connectTimer.unref) this._connectTimer.unref();
};

MqttClient.prototype._clearConnectTimer = function () {
  if (this._connectTimer) {
    clearTimeout(this._connectTimer);
    this._connectTimer = null;
  }
};

/** 关掉上一根 socket（建链时用，不触发重连） */
MqttClient.prototype._closeSocket = function () {
  const s = this.socket;
  this.socket = null;
  if (!s || typeof s.close !== "function") return;
  try {
    s.close({});
  } catch (e) {}
};

/** 建链/握手失败：只报一次错，然后交给上层重连 */
MqttClient.prototype._fail = function (reason) {
  if (this._failed) return;
  this._failed = true;
  this._clearConnectTimer();
  this.connected = false;
  this.online = false;
  this._stopTimer();
  this._closeSocket(); // 别把这根废掉的连接留在那儿
  this.emit("error", reason);
  this.emit("closed", { code: -1, reason: reason });
};

MqttClient.prototype._stopTimer = function () {
  if (this.timer) {
    clearInterval(this.timer);
    this.timer = null;
  }
};

MqttClient.prototype._startTimer = function () {
  const self = this;
  this._stopTimer();
  // KeepAlive 的一半发一次，和固件端一致
  this.timer = setInterval(function () {
    if (!self.connected) return;

    // 假在线检测：socket 还在、却长时间收不到任何报文（连 PINGRESP 都没有），
    // 说明这根连接其实已经废了。不主动报错的话，界面会一直显示「已连接」，
    // 用户点按钮只能看到 → 却永远等不到 ←。
    const idle = self.lastRx ? Date.now() - self.lastRx : 0;
    const limit = Math.max(20, self.keepalive * 2) * 1000;
    if (idle > limit) {
      self._fail("已 " + Math.round(idle / 1000) + " 秒没收到任何报文，判定连接失效（自动重连）");
      return;
    }

    self.socket.send({
      data: packet(T.PINGREQ << 4, []),
      fail: function () {}
    });
  }, Math.max(5, Math.floor(this.keepalive / 2)) * 1000);
};

MqttClient.prototype.subscribe = function (topic) {
  if (!this.connected) return false;
  const id = this.msgId++ & 0xffff;
  const body = [id >> 8, id & 0xff].concat(encodeString(topic), [0x00]); // QoS0
  this.socket.send({ data: packet((T.SUBSCRIBE << 4) | 0x02, body) });
  return true;
};

MqttClient.prototype.publish = function (topic, msg) {
  if (!this.connected) return false;
  const payload = utf8Encode(msg);
  const body = encodeString(topic).concat(payload);
  this.socket.send({ data: packet(T.PUBLISH << 4, body) });
  return true;
};

MqttClient.prototype.close = function () {
  this.manualClose = true;
  this._clearConnectTimer();
  this._stopTimer();
  if (this.socket && this.connected) {
    try {
      this.socket.send({ data: packet(T.DISCONNECT << 4, []) });
    } catch (e) {}
  }
  this._closeSocket();
  this.connected = false;
  this.online = false;
  this.buffer = [];
};

/* ---------------------------------------------------------- 流式组包解析 */

MqttClient.prototype._feed = function (data) {
  if (data === null || data === undefined) return;

  let bytes = null;
  const tag = Object.prototype.toString.call(data);

  if (tag === "[object ArrayBuffer]") {
    bytes = new Uint8Array(data);
  } else if (typeof ArrayBuffer !== "undefined" && ArrayBuffer.isView && ArrayBuffer.isView(data)) {
    // Uint8Array / Buffer / DataView
    bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  } else if (typeof data.byteLength === "number" && !data.buffer) {
    // 跨 realm 的 ArrayBuffer：instanceof 会失效，只能靠 byteLength 认
    bytes = new Uint8Array(data);
  } else if (typeof data === "string") {
    // 文本帧兜底（巴法云发的是二进制帧，正常不会走到这里）
    bytes = utf8Encode(data);
  } else {
    this.emit("log", "收到无法识别的帧：" + tag);
    return;
  }

  if (!bytes || !bytes.length) return;
  this.lastRx = Date.now();

  for (let i = 0; i < bytes.length; i++) this.buffer.push(bytes[i]);
  // 正常报文都在几十字节内，缓冲区涨到 64KB 说明已经解析错位，直接丢掉重来
  if (this.buffer.length > 65536) this.buffer = [];

  for (;;) {
    const pkt = this._takeOne();
    if (!pkt) break;
    this._dispatch(pkt);
  }
};

/** 从缓冲里取出一条完整报文，取不到返回 null */
MqttClient.prototype._takeOne = function () {
  const buf = this.buffer;
  if (buf.length < 2) return null;

  let remain = 0;
  let mult = 1;
  let n = 0;
  let i = 1;
  for (; i < buf.length && n < 4; i++) {
    const b = buf[i];
    remain += (b & 0x7f) * mult;
    mult *= 128;
    n++;
    if ((b & 0x80) === 0) break;
    if (n === 4) {
      // 非法剩余长度，丢掉首字节重同步
      buf.splice(0, 1);
      return null;
    }
  }
  if (i === buf.length && (buf[i - 1] & 0x80) !== 0) return null; // 长度字段没读完

  const headerLen = 1 + n;
  const total = headerLen + remain;
  if (buf.length < total) return null;

  const body = buf.slice(headerLen, total);
  const type = buf[0] >> 4;
  const flags = buf[0] & 0x0f;
  buf.splice(0, total);
  return { type, flags, body };
};

MqttClient.prototype._dispatch = function (pkt) {
  const self = this;

  if (pkt.type === T.CONNACK) {
    const code = pkt.body[1];
    if (code === 0) {
      this.emit("log", "MQTT CONNACK 成功");
      const topics = (this.cfg && this.cfg.topics) || [];
      for (let i = 0; i < topics.length; i++) this.subscribe(topics[i]);
      this._startTimer();
    } else {
      this.emit("error", "MQTT 被拒绝，返回码 " + code + connackHint(code));
      this.emit("closed", { code: code });
    }
    return;
  }

  if (pkt.type === T.SUBACK) {
    // body = [报文标识符 2 字节, 每个主题 1 字节返回码]
    const rc = pkt.body.length > 2 ? pkt.body[2] : 0;
    if (rc === 0x80) {
      // 巴法云：私钥/密钥与主题名不匹配时返回 0x80，此时收不到任何消息
      this.emit("error", "订阅被拒绝（0x80）：主题名和凭据不是同一个主题，检查设置页的主题名");
      this.emit("closed", { code: 0x80 });
      return;
    }
    this.online = true;
    this._clearConnectTimer();
    this.emit("log", "订阅成功: " + ((this.cfg && this.cfg.topics) || []).join(","));
    this.emit("online", null);
    return;
  }

  if (pkt.type === T.PUBLISH) {
    const b = pkt.body;
    const tlen = (b[0] << 8) | b[1];
    let off = 2;
    const topic = utf8Decode(b.slice(off, off + tlen));
    off += tlen;
    if (((pkt.flags & 0x06) >> 1) === 1) off += 2; // QoS1 的报文标识符
    const msg = utf8Decode(b.slice(off));
    this.emit("message", { topic: topic, msg: msg });
    return;
  }

  if (pkt.type === T.PINGRESP) {
    this.emit("pong", null);
    return;
  }
};

function connackHint(code) {
  const map = {
    1: "（协议版本不支持）",
    2: "（ClientID 不合法）",
    3: "（服务端不可用）",
    4: "（用户名或密码错误 → 去巴法云「连接密钥」页拿 appID / secretKey 填到设置里）",
    5: "（未授权 → 私钥/密钥填错了）"
  };
  return map[code] || "";
}

module.exports = {
  MqttClient: MqttClient,
  utf8Encode: utf8Encode,
  utf8Decode: utf8Decode
};
