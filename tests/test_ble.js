/**
 * tests/test_ble.js
 * 蓝牙扫描逻辑单元测试：用假的 wx 对象跑 utils/ble.js，不需要真机。
 *
 * 覆盖：
 *   1. 扫描前先初始化适配器（否则真机上必然 10000）
 *   2. 设备合并 / 按信号强度排序 / 名字后到能补上
 *   3. 设备名过滤只做标记、不再隐藏设备
 *   4. getBluetoothDevices 兜底轮询
 *   5. stopScan 会停 timer 并广播 discovering=false
 *   6. diagnose() 汇总系统蓝牙 / 定位 / 授权状态
 */

let checks = 0;
let failed = 0;

function ok(cond, label) {
  checks++;
  if (cond) return;
  failed++;
  console.log("  FAIL  " + label);
}

function eq(a, b, label) {
  ok(a === b, label + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")");
}

/* ----------------------------- 假 wx ----------------------------- */

let adapterOpened = false;
let discovering = false;
let foundCb = null;
const pullDevices = [];

global.wx = {
  openBluetoothAdapter(o) {
    adapterOpened = true;
    o.success && o.success({});
  },
  onBluetoothDeviceFound(cb) {
    foundCb = cb;
  },
  onBluetoothAdapterStateChange() {},
  onBLEConnectionStateChange() {},
  onBLECharacteristicValueChange() {},
  getBluetoothAdapterState(o) {
    o.success && o.success({ available: true, discovering: discovering });
  },
  startBluetoothDevicesDiscovery(o) {
    if (!adapterOpened) {
      o.fail && o.fail({ errCode: 10000, errMsg: "not init" });
      return;
    }
    discovering = true;
    o.success && o.success({});
  },
  stopBluetoothDevicesDiscovery(o) {
    discovering = false;
    o.complete && o.complete({});
  },
  getBluetoothDevices(o) {
    o.success && o.success({ devices: pullDevices.slice() });
  },
  getSystemSetting(o) {
    o.success && o.success({ bluetoothEnabled: true, locationEnabled: false });
  },
  getAppAuthorizeSetting(o) {
    o.success && o.success({ bluetoothAuthorized: "authorized", locationAuthorized: "denied" });
  },
  getSetting(o) {
    o.success && o.success({ authSetting: { "scope.bluetooth": true } });
  }
};

const bleMod = require("../mini-program/utils/ble.js");

function emitFound(list) {
  ok(!!foundCb, "onBluetoothDeviceFound 已注册");
  if (foundCb) foundCb({ devices: list });
}

/* ----------------------------- 用例 ----------------------------- */

(async function run() {
  const ble = new bleMod.BleChannel();
  const scans = [];
  ble.on("scan", (s) => scans.push(s));

  console.log("mini-program BLE scan unit test");

  ok(!ble.adapterReady, "初始状态适配器未初始化");

  // 1. 扫描会先把适配器打开
  await ble.startScan("jdy");
  ok(adapterOpened, "startScan 会自动 openBluetoothAdapter");
  ok(ble.adapterReady, "adapterReady = true");
  ok(ble.discovering, "discovering = true");
  eq(ble.filter, "jdy", "过滤词已记录");

  // 2. 收到设备：名字 / 信号强度 / 过滤标记
  emitFound([
    { deviceId: "AA", name: "JDY-24", RSSI: -70 },
    { deviceId: "BB", name: "Mi Band", RSSI: -45 }
  ]);
  eq(ble.devices.length, 2, "两个设备都进列表");
  eq(ble.devices[0].deviceId, "BB", "按 RSSI 从强到弱排序");
  eq(ble.devices.filter((d) => d.deviceId === "AA")[0].matched, true, "JDY-24 命中过滤");
  eq(ble.devices.filter((d) => d.deviceId === "BB")[0].matched, false, "Mi Band 不命中");

  // 3. 过滤不隐藏设备
  eq(ble.scanStat().total, 2, "scanStat.total 含不匹配设备");
  eq(ble.scanStat().matched, 1, "scanStat.matched = 1");

  // 4. 名字后到：先只有 deviceId，后来补上名字
  emitFound([{ deviceId: "CC", name: "", RSSI: -80 }]);
  emitFound([{ deviceId: "CC", name: "JDY-08", RSSI: -60 }]);
  const cc = ble.devices.filter((d) => d.deviceId === "CC")[0];
  eq(cc.name, "JDY-08", "名字后到能补上");
  eq(cc.matched, true, "补上名字后重新计算匹配");
  eq(ble.devices[0].deviceId, "BB", "补名字后仍按信号排序");

  // 5. 重复上报不产生重复条目
  emitFound([{ deviceId: "AA", name: "JDY-24", RSSI: -71 }]);
  eq(ble.devices.length, 3, "重复设备不重复入列");
  eq(ble.devices.filter((d) => d.deviceId === "AA")[0].rssi, -71, "RSSI 会更新");

  // 6. getBluetoothDevices 兜底
  pullDevices.push({ deviceId: "DD", name: "HM-10", RSSI: -55 });
  ble._pullDevices();
  eq(ble.devices.length, 4, "兜底轮询能补到设备");
  eq(ble.devices[1].deviceId, "DD", "兜底设备参与排序（-55 排在 -45 之后）");

  // 7. 停止扫描
  await ble.stopScan();
  eq(ble.discovering, false, "stopScan 后 discovering = false");
  eq(discovering, false, "已调用 stopBluetoothDevicesDiscovery");
  const last = scans[scans.length - 1];
  eq(last.discovering, false, "广播 discovering=false");
  eq(last.total, 4, "广播里带设备总数");

  // 8. 自检
  const diag = await ble.diagnose();
  eq(diag.adapterReady, true, "diagnose.adapterReady");
  eq(diag.bluetoothEnabled, true, "diagnose.bluetoothEnabled");
  eq(diag.locationEnabled, false, "diagnose.locationEnabled");
  eq(diag.bluetoothAuthorized, "authorized", "diagnose.bluetoothAuthorized");
  eq(diag.found, 4, "diagnose.found");

  // 9. 过滤词为空时全部匹配
  const ble2 = new bleMod.BleChannel();
  ble2.filter = "";
  ble2._mergeDevices([{ deviceId: "X", name: "", RSSI: -90 }]);
  eq(ble2.devices[0].matched, true, "过滤词为空时无名设备也算匹配");

  console.log(failed ? "FAILED  (" + failed + " / " + checks + ")" : "ALL PASS  (" + checks + " checks)");
  process.exit(failed ? 1 : 0);
})().catch(function (e) {
  console.log("  EXCEPTION " + (e && e.stack ? e.stack : e));
  process.exit(1);
});
