/**
 * tests/test_mqtt_js.js
 * 小程序端 MQTT 客户端（mini-program/utils/mqtt.js）的单元测试
 *
 *   运行： node tests/test_mqtt_js.js
 *
 * 重点：断言"生成的报文字节"与固件端 mqtt_packet.c 完全一致。
 *       两边是同一套协议的两份实现，字节对齐才不会出现"设备收不到命令"。
 *       tests/test_mqtt.c 里对相同的输入做了相同的断言。
 */

const path = require("path");
const assert = require("assert");

/* ---------------------------------------------------------------- wx 打桩 */
let sent = []; // 捕获所有发出的 ArrayBuffer
let openCb = null;
let msgCb = null;
let closeCb = null;

global.wx = {
  connectSocket() {
    return {
      send(opt) {
        sent.push(Buffer.from(opt.data));
        if (opt.success) opt.success();
      },
      close() {}
    };
  },
  onSocketOpen(cb) {
    openCb = cb;
  },
  onSocketMessage(cb) {
    msgCb = cb;
  },
  onSocketError() {},
  onSocketClose(cb) {
    closeCb = cb;
  }
};

const { MqttClient } = require(path.join(__dirname, "..", "mini-program", "utils", "mqtt.js"));

let checks = 0;
function eq(actual, expected, what) {
  checks++;
  const a = Buffer.isBuffer(actual) ? actual.toString("hex") : actual;
  const e = Buffer.isBuffer(expected) ? expected.toString("hex") : expected;
  assert.strictEqual(a, e, what + "\n  实际: " + a + "\n  期望: " + e);
}

function makeClient(cfg) {
  sent = [];
  openCb = msgCb = closeCb = null;
  const c = new MqttClient();
  c.connect(
    Object.assign(
      { url: "wss://bemfa.com:9504/wss", clientId: "abc", keepalive: 60, topics: ["lock001"] },
      cfg
    )
  );
  return c;
}

/** 模拟服务器发来一段字节 */
function deliver(bytes) {
  const b = Buffer.from(bytes);
  const ab = new ArrayBuffer(b.length);
  new Uint8Array(ab).set(b);
  msgCb({ data: ab });
}

/* ================================================================== 用例 */

function testConnectFrame() {
  const c = makeClient();
  openCb(); // WebSocket 打开后才发 CONNECT

  // 10 0F | 00 04 "MQTT" 04 02 00 3C | 00 03 "abc"
  //   固定头 剩余长度15+可变头10+载荷5=15
  eq(sent[0], "100f00044d5154540402003c0003616263", "CONNECT 报文字节");
  console.log("  connect frame          ok");
  return c;
}

function testSubscribeFrame() {
  const c = makeClient();
  openCb();
  sent = [];
  c.subscribe("lock001");
  // 82 0C | 00 01(报文标识符) 00 07 "lock001"(QoS0)
  eq(sent[0], "820c000100076c6f636b30303100", "SUBSCRIBE 报文字节");
  console.log("  subscribe frame        ok");
  return c;
}

function testPublishFrame() {
  const c = makeClient();
  openCb();
  sent = [];
  c.publish("lock001/set", "unlock");
  // 30 12 | 00 0B "lock001/set" "unlock"
  eq(sent[0], "3013000b6c6f636b3030312f736574756e6c6f636b", "PUBLISH 报文字节");
  console.log("  publish frame          ok");
}

function testHandshakeTriggersSubscribe() {
  const c = makeClient();
  openCb();
  sent = [];
  deliver([0x20, 0x02, 0x00, 0x00]); // CONNACK 成功
  assert.strictEqual(c.online, false, "收到 CONNACK 只是开始订阅，还不算在线");
  checks++;
  eq(sent[0], "820c000100076c6f636b30303100", "CONNACK 后自动发出的 SUBSCRIBE 字节");
  deliver([0x90, 0x03, 0x00, 0x01, 0x00]); // SUBACK
  assert.strictEqual(c.online, true, "收到 SUBACK 后应该在线");
  checks++;
  console.log("  handshake->subscribe   ok");
  return c;
}

function testRejectedConnack() {
  let errMsg = null;
  const c = new MqttClient();
  c.on("error", (m) => (errMsg = m));
  sent = [];
  openCb = msgCb = null;
  c.connect({ url: "wss://x", clientId: "abc", keepalive: 60, topics: ["lock001"] });
  openCb();
  deliver([0x20, 0x02, 0x00, 0x05]); // 未授权
  assert.ok(errMsg && errMsg.indexOf("5") >= 0, "应该报出返回码 5");
  checks++;
  console.log("  rejected connack       ok");
}

function testDecodePublish() {
  const c = makeClient();
  openCb();
  const got = [];
  c.on("message", (m) => got.push(m));

  // body = 2 + 11("lock001/set") + 12("state=unlock") = 25 = 0x19
  deliver([0x30, 0x19, 0x00, 0x0b].concat(Array.from(Buffer.from("lock001/set")))
    .concat(Array.from(Buffer.from("state=unlock"))));

  assert.strictEqual(got.length, 1, "应该解出 1 条消息");
  checks++;
  assert.strictEqual(got[0].topic, "lock001/set");
  checks++;
  assert.strictEqual(got[0].msg, "state=unlock");
  checks++;
  console.log("  decode publish         ok");
}

function testSplitAndSticky() {
  const c = makeClient();
  openCb();
  const got = [];
  c.on("message", (m) => got.push(m.msg));
  c.on("pong", () => got.push("PONG"));

  const publish = [0x30, 0x0f, 0x00, 0x07].concat(Array.from(Buffer.from("lock001")))
    .concat(Array.from(Buffer.from("unlock")));

  // 1) 一个字节一个字节喂（拆包）
  const all = publish.concat([0xd0, 0x00]); // 后面再粘一帧 PINGRESP
  for (let i = 0; i < all.length; i++) deliver([all[i]]);
  assert.deepStrictEqual(got, ["unlock", "PONG"], "拆包+粘包都应正确还原");
  checks++;

  // 2) 一次喂两帧（粘包）
  got.length = 0;
  deliver(publish.concat([0xd0, 0x00])); // PINGRESP
  assert.deepStrictEqual(got, ["unlock", "PONG"]);
  checks++;
  console.log("  split / sticky         ok");
}

function testUtf8Roundtrip() {
  const m = require(path.join(__dirname, "..", "mini-program", "utils", "mqtt.js"));
  const s = "锁 lock001 状态=已开锁";
  eq(Buffer.from(m.utf8Encode(s)), Buffer.from(s, "utf8"), "utf8Encode");
  assert.strictEqual(m.utf8Decode(m.utf8Encode(s)), s, "utf8Decode 应能还原");
  checks++;
  console.log("  utf8 roundtrip         ok");
}

/* ================================================================== main */

console.log("mini-program MQTT client self-test");
testConnectFrame();
testSubscribeFrame();
testPublishFrame();
testHandshakeTriggersSubscribe();
testRejectedConnack();
testDecodePublish();
testSplitAndSticky();
testUtf8Roundtrip();

console.log("\nALL PASS  (" + checks + " checks)");

// 客户端里的心跳定时器会让 Node 事件循环一直不退出，显式结束进程
process.exit(0);
