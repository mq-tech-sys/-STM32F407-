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
    connected: false,
    online: false,
    lockState: "", // "" 未知 | "lock" 已上锁 | "unlock" 已开锁
    topic: "",
    bleName: "",
    logs: [],
    devices: [],
    scanning: false,
    scanStat: { total: 0, named: 0, matched: 0, filter: "", error: "" },
    bleServiceHint: ""
  },

  onLoad() {
    const self = this;
    const c = ch();

    this.setData({ mode: c.mode, online: c.online, connected: c.connected });

    this._hStatus = function (s) {
      self.setData({ mode: s.mode, connected: s.connected, online: s.online });
    };
    this._hLog = function (text) {
      self.addLog(text, "");
    };
    this._hErr = function (text) {
      self.addLog(text, "err");
    };
    this._hMsg = function (m) {
      self.onDeviceMessage(m.msg);
    };
    this._hDev = function () {
      self.syncDevices();
    };
    this._hScan = function (st) {
      const was = self.data.scanning;
      const now = !!(st && st.discovering);
      self.setData({ scanning: now, scanStat: st || {} });
      self.syncDevices();
      // 扫描刚结束：给一句结论，别让人对着空列表猜
      if (was && !now) {
        const s = st || {};
        if (s.error) {
          self.addLog(s.error, "err");
        } else if (!s.total) {
          self.addLog(
            "扫描结束：一个设备都没发现。去「设置 → 蓝牙自检」看是权限、系统蓝牙还是附近真没有模块",
            "err"
          );
        } else {
          self.addLog(
            "扫描结束：发现 " + s.total + " 个设备，其中 " + s.named + " 个有名字",
            ""
          );
        }
      }
    };

    c.on("status", this._hStatus);
    c.on("log", this._hLog);
    c.on("error", this._hErr);
    c.on("message", this._hMsg);
    c.on("device", this._hDev);
    c.on("scan", this._hScan);

    this.addLog("通道：" + (c.mode === "cloud" ? "云端 MQTT" : "蓝牙直连"), "");
    this.syncHeader();
    this.autoConnect();
  },

  onShow() {
    this.setData({ theme: theme.sync() });
    this.syncHeader();
  },

  /** 页头副标题：云端显示主题名，蓝牙显示设备名 */
  syncHeader() {
    const c = ch();
    const ble = c.cfg && c.cfg.ble;
    this.setData({
      topic: (c.cfg && c.cfg.bemfa && c.cfg.bemfa.topic) || "",
      bleName: (ble && ble.deviceName) || ""
    });
  },

  /** 把蓝牙模块里缓存的设备列表同步到页面（不再按名字过滤，只打标记） */
  syncDevices() {
    const c = ch();
    this.setData({ devices: (c.ble.devices || []).slice() });
  },

  onToggleTheme() {
    this.setData({ theme: theme.toggle() });
  },

  onUnload() {
    // 不解绑：通道是全局的，切到「功能」页还要继续收发
  },

  /* ------------------------------------------------------------ 连接管理 */

  autoConnect() {
    const self = this;
    const c = ch();

    if (c.mode === "cloud") {
      this.addLog("正在连接云端…", "");
      c.connectCloud();
      return;
    }

    c.openBle()
      .then(function () {
        return c.connect();
      })
      .then(function () {
        if (!c.ble.online) self.addLog("未找到已保存的设备，请点「扫描」选择", "");
      })
      .catch(function (e) {
        self.addLog(e.message || "蓝牙初始化失败", "err");
      });
  },

  onMode(e) {
    const self = this;
    const mode = e.currentTarget.dataset.mode;
    const c = ch();
    c.switchMode(mode);
    this.setData({ mode: mode, devices: [], lockState: "", online: false });
    this.addLog("切换到" + (mode === "cloud" ? "云端 MQTT" : "蓝牙直连"), "");

    if (mode === "cloud") {
      c.connectCloud();
    } else {
      c.openBle()
        .then(function () {
          return c.connect();
        })
        .catch(function (err) {
          self.addLog(err.message || "蓝牙初始化失败", "err");
        });
    }
  },

  onReconnect() {
    const c = ch();
    if (c.mode === "cloud") c.connectCloud();
    else this.autoConnect();
  },

  onScan() {
    const self = this;
    const c = ch();
    if (this.data.scanning) {
      c.ble.stopScan();
      this.setData({ scanning: false });
      return;
    }
    this.setData({
      devices: [],
      scanning: true,
      scanStat: { total: 0, named: 0, matched: 0, filter: "", error: "" }
    });
    c.scanBle()
      .then(function () {
        self.addLog("开始扫描（12 秒），附近所有 BLE 设备都会列出来…", "");
      })
      .catch(function (e) {
        self.setData({ scanning: false });
        self.addLog(e.message || "扫描失败", "err");
      });
  },

  /** 扫描为空时的一键排查：把系统蓝牙/定位/授权状态打到日志里 */
  onScanHelp() {
    const self = this;
    const c = ch();
    c.diagnoseBle()
      .then(function (d) {
        const yes = (v) => (v ? "是" : "否");
        self.addLog("— 蓝牙自检 —", "");
        self.addLog("适配器已初始化：" + yes(d.adapterReady) + "，系统蓝牙可用：" + yes(d.available), "");
        self.addLog(
          "系统蓝牙开关：" + (d.bluetoothEnabled === null ? "未知" : yes(d.bluetoothEnabled)) +
            "，定位开关：" + (d.locationEnabled === null ? "未知" : yes(d.locationEnabled)),
          ""
        );
        self.addLog(
          "小程序授权：蓝牙 " + (d.bluetoothAuthorized || d.scopeBluetooth || "未知") +
            "，定位 " + (d.locationAuthorized || d.scopeLocation || "未知"),
          ""
        );
        self.addLog("已发现设备：" + d.found + " 个" + (d.error ? "，上次错误：" + d.error : ""), "");
      })
      .catch(function (e) {
        self.addLog(e.message || "自检失败", "err");
      });
  },

  onPickDevice(e) {
    const self = this;
    const c = ch();
    const deviceId = e.currentTarget.dataset.id;
    c.ble.stopScan();
    this.setData({ scanning: false });
    this.addLog("正在连接…", "");

    c.connectBle(deviceId)
      .then(function (info) {
        self.setData({
          bleServiceHint: info.serviceId + " / " + info.writeCharId
        });
      })
      .catch(function (err) {
        self.addLog(err.message || "连接失败", "err");
      });
  },

  /* ------------------------------------------------------------ 门锁操作 */

  onUnlock() {
    this.send("unlock");
  },

  onLock() {
    this.send("close");
  },

  onRefresh() {
    this.send("state");
  },

  send(line) {
    const c = ch();
    if (!c.isOnline()) {
      this.addLog("还没连上，先点右上角重连 / 扫描设备", "err");
      return;
    }
    c.send(line);
  },

  /* ------------------------------------------------------------ 消息处理 */

  onDeviceMessage(msg) {
    if (!msg) return;

    if (msg.indexOf("state=") === 0) {
      const st = msg.slice(6).trim();
      this.setData({ lockState: st === "unlock" ? "unlock" : "lock" });
      this.addLog("← 门锁状态：" + (st === "unlock" ? "已开锁" : "已上锁"), "rx");
      return;
    }

    if (msg.indexOf("num=") === 0) {
      const p = msg.slice(4).split(",");
      this.addLog("← " + (p[0] === "fp" ? "指纹数量" : "卡片数量") + "：" + p[1], "rx");
      return;
    }

    // acks / pong / err / help 等一律直接展示
    this.addLog("← " + msg, "rx");
  },

  addLog(text, cls) {
    const logs = this.data.logs.slice();
    logs.unshift({ time: now(), text: text, cls: cls || "" });
    if (logs.length > 40) logs.length = 40;
    this.setData({ logs: logs });
  },

  onClearLog() {
    this.setData({ logs: [] });
  }
});
