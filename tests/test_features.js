/**
 * tests/test_features.js
 * 功能页（列表 / 单项删除）逻辑单元测试。
 *
 * 做法：用假的 Page / getApp / wx 把 pages/features/features.js 加载进来，
 * 直接调它的方法、喂它设备会回的消息，验证：
 *   1. 点「刷新列表」确实下发 fp_list / card_list
 *   2. fp=<id> ... fpend=<n> 能拼成列表，且不把中间行写进应答日志
 *   3. card=<索引>,<UID> ... cardend=<n> 能排序、大写化
 *   4. 点某一项的「删除」会弹确认，确认后下发 fp_del:<id> / card_del:<idx>
 *   5. ack=fp_del_ok 之后会自动重新拉列表
 *   6. 设备回 err=busy 时列表 loading 会被清掉（不会永远转圈）
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

/* ----------------------------- 假运行环境 ----------------------------- */

const sent = [];
const listeners = {};

const fakeChannel = {
  mode: "cloud",
  online: true,
  cfg: {},
  on(evt, cb) {
    (listeners[evt] = listeners[evt] || []).push(cb);
  },
  isOnline() {
    return true;
  },
  send(line) {
    sent.push(line);
    return true;
  }
};

let pageDef = null;
global.Page = function (def) {
  pageDef = def;
};

global.getApp = function () {
  return { globalData: { channel: fakeChannel } };
};

let modalCalls = [];
global.wx = {
  getStorageSync() {
    return "light";
  },
  setStorageSync() {},
  setNavigationBarColor() {},
  setBackgroundColor() {},
  setTabBarStyle() {},
  showToast() {},
  switchTab() {},
  showModal(opt) {
    modalCalls.push(opt);
  }
};

require("../mini-program/pages/features/features.js");

function newPage() {
  const page = Object.assign({}, pageDef);
  page.data = JSON.parse(JSON.stringify(pageDef.data));
  page.setData = function (patch) {
    Object.assign(this.data, patch);
  };
  return page;
}

function deliver(msg) {
  (listeners.message || []).forEach(function (cb) {
    cb({ topic: "x", msg: msg });
  });
}

function lastSent() {
  return sent[sent.length - 1];
}

(async function run() {
  console.log("mini-program features page unit test");

  const page = newPage();
  page.onLoad();

  /* ---------- 1. 刷新指纹列表 ---------- */
  sent.length = 0;
  page.onFpList();
  eq(lastSent(), "fp_list", "refresh fingerprint list sends fp_list");
  eq(page.data.fpLoading, true, "fpLoading = true while waiting");

  deliver("fp=0");
  deliver("fp=2");
  eq(page.data.fpList.length, 0, "partial lines do not update list yet");
  deliver("fpend=2");
  eq(page.data.fpList.length, 2, "two fingerprints listed");
  eq(page.data.fpList[1].id, "2", "fingerprint id parsed");
  eq(page.data.fpCount, "2", "fingerprint count synced from fpend");
  eq(page.data.fpLoading, false, "fpLoading cleared after fpend");

  const fpAnswers = page.data.answers.filter(function (a) {
    return a.text.indexOf("指纹列表") === 0;
  });
  eq(fpAnswers.length, 1, "list produces exactly one summary line");
  ok(
    page.data.answers.every(function (a) {
      return a.text.indexOf("fp=") !== 0;
    }),
    "raw fp= lines are not logged"
  );

  /* ---------- 2. 刷新卡列表 ---------- */
  sent.length = 0;
  page.onCardList();
  eq(lastSent(), "card_list", "refresh card list sends card_list");

  deliver("card=1,11223344");
  deliver("card=0,aabbccdd");
  deliver("cardend=2");
  eq(page.data.cardList.length, 2, "two cards listed");
  eq(page.data.cardList[0].idx, 0, "cards sorted by index");
  eq(page.data.cardList[0].uid, "AABBCCDD", "uid uppercased");

  /* ---------- 3. 单项删除：必须先确认 ---------- */
  modalCalls = [];
  sent.length = 0;
  page.onFpDeleteItem({ currentTarget: { dataset: { id: "2" } } });
  eq(modalCalls.length, 1, "delete asks for confirmation");
  eq(sent.length, 0, "nothing sent before confirmation");

  modalCalls[0].success({ confirm: false });
  eq(sent.length, 0, "cancel sends nothing");

  page.onFpDeleteItem({ currentTarget: { dataset: { id: "2" } } });
  modalCalls[1].success({ confirm: true });
  eq(lastSent(), "fp_del:2", "confirmed delete sends fp_del:<id>");

  /* ---------- 4. 删除成功后自动刷新 ---------- */
  sent.length = 0;
  deliver("ack=fp_del_ok");
  await new Promise(function (r) {
    setTimeout(r, 600);
  });
  eq(sent[0], "fp_list", "auto refresh after fp_del_ok");

  /* ---------- 5. 卡删除 ---------- */
  modalCalls = [];
  sent.length = 0;
  page.onCardDeleteItem({ currentTarget: { dataset: { idx: 1 } } });
  modalCalls[0].success({ confirm: true });
  eq(lastSent(), "card_del:1", "confirmed delete sends card_del:<idx>");

  /* ---------- 6. 设备忙 / 无响应 ---------- */
  page.setData({ fpLoading: true, cardLoading: true });
  deliver("err=busy");
  eq(page.data.fpLoading, false, "err=busy clears fp loading");
  eq(page.data.cardLoading, false, "err=busy clears card loading");
  eq(
    page.data.answers[0].text,
    "设备正忙（正在录入或刷卡），稍后再试",
    "err=busy shows a readable hint"
  );

  /* ---------- 7. 空列表 ---------- */
  page.onFpList();
  deliver("fpend=0");
  eq(page.data.fpList.length, 0, "empty list stays empty");
  eq(page.data.fpCount, "0", "empty list reports 0");

  /* ---------- 8. 畸形行不该崩 ---------- */
  deliver("card=oops");
  deliver("fpend=");
  eq(page.data.fpList.length, 0, "malformed trailing line ignored");

  console.log(
    failed
      ? "FAILED  (" + failed + " / " + checks + ")"
      : "ALL PASS  (" + checks + " checks)"
  );
  process.exit(failed ? 1 : 0);
})().catch(function (e) {
  console.log("  EXCEPTION " + (e && e.stack ? e.stack : e));
  process.exit(1);
});
