/**
 * 把小程序页面渲染成静态 HTML / PNG，用于 README 截图。
 *
 *   node tools/render_preview.js                    # 渲染全部页面（浅色 + 深色各一张）
 *   node tools/render_preview.js --page index       # 只渲染控制页
 *   node tools/render_preview.js --theme dark       # 只渲染深色
 *   node tools/render_preview.js --png              # 顺带截成 png（默认输出到 mini-program/screenshots）
 *
 * 关键点：**用的是小程序自己的 app.wxss + 页面 wxss**，只做 rpx -> px 的 0.5 折算，
 * 所以出来的样子和真机/开发者工具一致；数据用 PREVIEW_DATA 里的假数据。
 * 改了 wxml/wxss 之后重跑一次，截图就同步了。
 */

const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawn } = require("child_process");

const ROOT = path.resolve(__dirname, "..");
const MP = path.join(ROOT, "mini-program");

const EDGE = [
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"
].find((p) => fs.existsSync(p));

const ARGV = process.argv.slice(2);
const arg = (name, def) => {
  const i = ARGV.indexOf("--" + name);
  return i >= 0 && ARGV[i + 1] && !ARGV[i + 1].startsWith("--") ? ARGV[i + 1] : def;
};
const flag = (name) => ARGV.includes("--" + name);

const OUT_DIR = path.resolve(ROOT, arg("out", "mini-program/screenshots"));
const ONLY_PAGE = arg("page", "");
const ONLY_THEME = arg("theme", "");
const WANT_PNG = flag("png");

/* ------------------------------------------------------------------ 假数据 */

const LOG = [
  { time: "10:31:02", text: "← state=lock", cls: "rx" },
  { time: "10:30:58", text: "← ack=close", cls: "rx" },
  { time: "10:30:58", text: "→ close", cls: "" },
  { time: "10:30:41", text: "← ack=unlock", cls: "rx" },
  { time: "10:30:41", text: "→ unlock", cls: "" },
  { time: "10:30:12", text: "订阅成功 lock000", cls: "" }
];

const PREVIEW_DATA = {
  index: {
    theme: "light",
    mode: "cloud",
    connected: true,
    online: true,
    lockState: "unlock",
    topic: "lock000",
    bleName: "",
    logs: LOG,
    devices: [],
    scanning: false,
    scanStat: { total: 0, named: 0, matched: 0, filter: "", error: "" },
    bleServiceHint: ""
  },
  features: {
    theme: "light",
    online: true,
    fpCount: 3,
    cardCount: 2,
    fpList: [{ id: 1 }, { id: 2 }, { id: 5 }],
    cardList: [
      { idx: 0, uid: "A1B2C3D4" },
      { idx: 1, uid: "5E6F7A8B" }
    ],
    fpLoading: false,
    cardLoading: false,
    password: "",
    newPassword: "",
    answers: [
      { time: "10:31:20", text: "ack=card_del_ok" },
      { time: "10:31:20", text: "→ card_del:1 已删除，刷新列表" },
      { time: "10:31:02", text: "state=lock" },
      { time: "10:30:41", text: "ack=unlock" }
    ]
  },
  settings: {
    theme: "light",
    url: "wss://bemfa.com:9504/wss",
    topic: "lock000",
    clientId: "wxmp_lock01",
    // 截图里只放占位符，真实 appID/secretKey 一律不进仓库
    username: "beid_your_appid_here",
    password: "your_secret_key_here",
    keepalive: 60,
    nameFilter: "",
    serviceId: "",
    characteristicId: "",
    uuidList: [],
    bleDiag: [],
    fwUid: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    fwTopic: "lock000"
  }
};

// 输出文件名：控制页的目录叫 index，截图叫 control 更好认
const OUT_NAME = { index: "control" };

const TABS = [
  { key: "index", label: "控制", icon: "tab-control" },
  { key: "features", label: "功能", icon: "tab-features" },
  { key: "settings", label: "设置", icon: "tab-settings" }
];

/* ------------------------------------------------------------------ WXML 解析 */

const VOID_TAGS = new Set(["input", "image", "icon"]);

function parseWxml(src) {
  const root = { type: "root", children: [] };
  const stack = [root];
  // 注释先剔掉，否则 <!-- 页头 --> 会被当成文本渲染出来
  src = src.replace(/<!--[\s\S]*?-->/g, "");
  const re = /<\/?([a-zA-Z][\w-]*)((?:"[^"]*"|'[^']*'|[^>"'])*?)(\/?)>|([^<]+)/g;
  let m;
  while ((m = re.exec(src))) {
    const [all, tag, attrsRaw, selfClose, text] = m;
    if (text !== undefined) {
      stack[stack.length - 1].children.push({ type: "text", text });
      continue;
    }
    if (all.startsWith("</")) {
      if (stack.length > 1) stack.pop();
      continue;
    }
    const node = { type: "el", tag, attrs: parseAttrs(attrsRaw || ""), children: [] };
    stack[stack.length - 1].children.push(node);
    if (!selfClose && !VOID_TAGS.has(tag)) stack.push(node);
  }
  return root;
}

function parseAttrs(raw) {
  const attrs = {};
  const re = /([a-zA-Z_:][\w:.-]*)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'))?/g;
  let m;
  while ((m = re.exec(raw))) {
    attrs[m[1]] = m[2] !== undefined ? m[2] : m[3] !== undefined ? m[3] : "true";
  }
  return attrs;
}

/* ------------------------------------------------------------------ 表达式求值 */

const exprCache = new Map();

function evalExpr(expr, scope) {
  const code = expr.replace(/^\{\{/, "").replace(/\}\}$/, "").trim();
  if (!code) return "";
  let fn = exprCache.get(code);
  if (!fn) {
    try {
      fn = new Function("$s", "with ($s) { return (" + code + "); }");
    } catch (e) {
      fn = () => "";
    }
    exprCache.set(code, fn);
  }
  try {
    const v = fn(scope);
    return v === undefined || v === null ? "" : v;
  } catch (e) {
    return "";
  }
}

const esc = (s) =>
  String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

/** 文本节点：{{}} 求值并转义，其余原样 */
function interpText(text, scope) {
  return text.replace(/\{\{([\s\S]*?)\}\}/g, (_, e) => esc(evalExpr("{{" + e + "}}", scope)));
}

/* ------------------------------------------------------------------ 渲染 */

function renderChildren(children, scope) {
  let out = "";
  let prevIf = null;
  for (const node of children) {
    if (node.type === "text") {
      out += interpText(node.text, scope);
      continue;
    }
    const a = node.attrs;

    if ("wx:if" in a && !truthy(evalExpr(a["wx:if"], scope))) {
      prevIf = false;
      continue;
    }
    if ("wx:if" in a) prevIf = true;
    if ("wx:elif" in a) {
      if (prevIf) continue;
      if (!truthy(evalExpr(a["wx:elif"], scope))) {
        prevIf = false;
        continue;
      }
      prevIf = true;
    }
    if ("wx:else" in a) {
      if (prevIf) continue;
      prevIf = null;
    }

    if ("wx:for" in a) {
      const list = evalExpr(a["wx:for"], scope) || [];
      const itemName = a["wx:for-item"] || "item";
      const idxName = a["wx:for-index"] || "index";
      (Array.isArray(list) ? list : []).forEach((item, i) => {
        const child = Object.assign({}, scope);
        child[itemName] = item;
        child[idxName] = i;
        out += renderEl(node, child);
      });
      continue;
    }

    out += renderEl(node, scope);
  }
  return out;
}

const truthy = (v) => !(v === "" || v === 0 || v === false || v === null || v === undefined);

const toPx = (css) => css.replace(/(-?\d*\.?\d+)rpx/g, (_, n) => `${(parseFloat(n) / 2).toFixed(2)}px`);

function renderEl(node, scope) {
  const tag = node.tag;
  const a = node.attrs;

  if (tag === "block") return renderChildren(node.children, scope);

  const cls = a.class ? interpText(a.class, scope).trim() : "";
  // 行内 style 里也有 rpx，不折算浏览器直接忽略（间距就没了）
  let style = a.style ? toPx(interpText(a.style, scope)) : "";
  if ("hidden" in a && truthy(evalExpr(a.hidden, scope))) {
    style += ";display:none";
  }

  const extra = (cls ? ` class="${esc(cls)}"` : "") + (style ? ` style="${esc(style)}"` : "");

  if (tag === "input") {
    const val = a.value ? interpText(a.value, scope) : "";
    const ph = a.placeholder ? interpText(a.placeholder, scope) : "";
    const isPwd = a.password === "{{true}}" || a.password === "true";
    const text = val ? (isPwd ? "•".repeat(String(val).length) : val) : ph;
    const cls2 = esc((cls + (val ? "" : " ph")).trim());
    return `<div class="${cls2}"${style ? ` style="${esc(style)}"` : ""}>${text}</div>`;
  }

  const map = { view: "div", text: "span", button: "button" };
  const html = map[tag] || "div";

  let dis = "";
  if (tag === "button" && "disabled" in a && truthy(evalExpr(a.disabled, scope))) dis = " disabled";

  return `<${html}${extra}${dis}>${renderChildren(node.children, scope)}</${html}>`;
}

/* ------------------------------------------------------------------ CSS */

function loadCss(page) {
  const files = [path.join(MP, "app.wxss"), path.join(MP, "pages", page, page + ".wxss")];
  let css = files.filter((f) => fs.existsSync(f)).map((f) => fs.readFileSync(f, "utf8")).join("\n");
  css = css.replace(/(-?\d*\.?\d+)rpx/g, (_, n) => `${(parseFloat(n) / 2).toFixed(2)}px`);
  css = css.replace(/^\s*page\s*\{/m, "body {");
  return css;
}

const phoneCss = `
html, body { margin: 0; padding: 0; background: #eef1f6; }
body { width: 375px; }
.phone { width: 375px; overflow: hidden; background: #fff; }
.phone .app { min-height: 0; padding-bottom: 18px; }
/* 小程序 button 默认 block 撑满，HTML 里补一下 */
.phone button { font: inherit; color: inherit; border: 0; background: transparent;
  padding: 0; margin: 0; width: 100%; box-sizing: border-box; text-align: center;
  display: flex; align-items: center; justify-content: center; }
.phone button.btn { width: 100%; }
.phone .btn-row button.btn { flex: 1; width: auto; }
/* tabBar（微信只认 png，这里直接引用工程里的图标） */
.tabbar { display: flex; background: var(--card); border-top: 1px solid var(--line); }
.tabbar .tb { flex: 1; padding: 6px 0 4px; display: flex; flex-direction: column;
  align-items: center; gap: 2px; font-size: 10px; color: var(--text-3); }
.tabbar .tb img { width: 22px; height: 22px; }
.tabbar .tb.on { color: var(--primary); }
`;

function tabbar(page, theme) {
  return (
    `<div class="tabbar">` +
    TABS.map((t) => {
      const on = t.key === page;
      const file = path.join(MP, "images", "tabbar", `${t.icon}${on ? "-on" : ""}.png`);
      const b64 = fs.existsSync(file) ? fs.readFileSync(file).toString("base64") : "";
      const src = b64 ? `data:image/png;base64,${b64}` : "";
      return `<div class="tb ${on ? "on" : ""}">${
        src ? `<img src="${src}" alt="">` : ""
      }<span>${t.label}</span></div>`;
    }).join("") +
    `</div>`
  );
}

function buildHtml(page, theme) {
  const wxml = fs.readFileSync(path.join(MP, "pages", page, page + ".wxml"), "utf8");
  const tree = parseWxml(wxml);
  const data = Object.assign({}, PREVIEW_DATA[page], { theme });
  const body = renderChildren(tree.children, data);

  return `<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>${page}-${theme}</title>
<style>
${phoneCss}
${loadCss(page)}
</style></head>
<body>
<div class="phone theme-${theme}">${body}${tabbar(page, theme)}</div>
<script>
window.addEventListener("load", function () {
  document.title = "H=" + Math.ceil(document.documentElement.scrollHeight);
});
</script>
</body></html>`;
}

/* ------------------------------------------------------------------ 截图 */

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/**
 * 用 Chrome DevTools 协议截整页图。
 *
 * 为什么不用 `--screenshot`：无头 Edge 在某些机器上只截「视口」，页面长了就被截断；
 * 而且同一台机器上重复调用会复用已存在的浏览器实例，参数直接失效。
 * 走 CDP 可以自己量内容高度（Page.getLayoutMetrics），一次拿到完整长图。
 */
async function cdpShot(url, pngPath, width = 375, scale = 1) {
  const port = 9400 + Math.floor(Math.random() * 400);
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), "wxp-")).replace(/\\/g, "/");
  const child = spawn(EDGE, [
    "--headless=new", "--disable-gpu", "--no-sandbox", "--hide-scrollbars",
    `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, "about:blank"
  ], { stdio: "ignore" });

  const base = `http://127.0.0.1:${port}`;
  let ok = false;
  for (let i = 0; i < 100 && !ok; i++) {
    try {
      await fetch(base + "/json/version");
      ok = true;
    } catch (e) {
      await sleep(100);
    }
  }
  if (!ok) throw new Error("浏览器没起来");

  const res = await fetch(`${base}/json/new?${encodeURIComponent("about:blank")}`, { method: "PUT" });
  const target = await res.json();
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r, j) => {
    ws.onopen = r;
    ws.onerror = j;
  });

  let id = 0;
  const pending = new Map();
  const events = [];
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(msg.error.message)) : resolve(msg.result);
    } else if (msg.method) {
      events.push(msg.method);
    }
  };
  const send = (method, params) =>
    new Promise((resolve, reject) => {
      const mid = ++id;
      pending.set(mid, { resolve, reject });
      ws.send(JSON.stringify({ id: mid, method, params: params || {} }));
    });

  await send("Page.enable");
  await send("Emulation.setDeviceMetricsOverride", {
    width, height: 812, deviceScaleFactor: scale, mobile: true
  });
  await send("Page.navigate", { url });
  for (let i = 0; i < 100 && !events.includes("Page.loadEventFired"); i++) await sleep(100);
  await sleep(350); // 等字体和 mask 图标

  const ev = await send("Runtime.evaluate", {
    expression: "Math.ceil(document.querySelector('.phone').getBoundingClientRect().height)",
    returnByValue: true
  });
  const height = Math.max(200, ev.result.value || 812);
  const shot = await send("Page.captureScreenshot", {
    format: "png",
    captureBeyondViewport: true,
    clip: { x: 0, y: 0, width, height, scale }
  });

  fs.writeFileSync(pngPath, Buffer.from(shot.data, "base64"));
  ws.close();
  child.kill();
  return { width, height };
}

/* ------------------------------------------------------------------ main */

(async function main() {
  fs.mkdirSync(OUT_DIR, { recursive: true });

  const pages = ONLY_PAGE ? [ONLY_PAGE] : Object.keys(PREVIEW_DATA);
  const themes = ONLY_THEME ? [ONLY_THEME] : ["light", "dark"];

  for (const page of pages) {
    for (const theme of themes) {
      const base = path.join(OUT_DIR, `${OUT_NAME[page] || page}-${theme}`);
      const htmlPath = base + ".html";
      fs.writeFileSync(htmlPath, buildHtml(page, theme), "utf8");

      let info = "";
      if (WANT_PNG && EDGE) {
        const r = await cdpShot("file:///" + htmlPath.replace(/\\/g, "/"), base + ".png");
        info = ` -> ${OUT_NAME[page] || page}-${theme}.png (${r.width}x${r.height})`;
        fs.unlinkSync(htmlPath);
      }
      console.log(`rendered ${page}/${theme}${info}`);
    }
  }

  if (!WANT_PNG) console.log(`\nHTML 输出目录：${OUT_DIR}（加 --png 可顺带截成图片）`);
  if (WANT_PNG && !EDGE) console.log("没找到 Edge/Chrome，只输出了 HTML。");
})();
