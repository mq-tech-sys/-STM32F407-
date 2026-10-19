/**
 * tests/test_ui.js
 * 小程序界面的静态检查 —— 本机没有微信开发者工具 CLI，用它兜住
 * 「改完界面一打开就白屏 / 点了没反应 / 图标不显示」这类低级错。
 *
 * 检查项：
 *   1. 每个页面的 wxml 标签是否配对
 *   2. bindtap 绑定的方法在对应 .js 里是否存在
 *   3. wxml 里用到的 i-xxx 图标类是否在 app.wxss 里定义
 *   4. 深浅两套主题的 CSS 变量是否齐全、var() 引用的变量是否都有定义
 *   5. app.json 里登记的页面文件与 tabBar 图标是否都存在
 *
 * 运行： node tests/test_ui.js
 */

const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..", "mini-program");

let checks = 0;
let failed = 0;

function ok(cond, msg) {
  checks++;
  if (!cond) {
    failed++;
    console.error("  FAIL  " + msg);
  }
}

function read(p) {
  return fs.readFileSync(p, "utf8");
}

function exists(p) {
  try {
    fs.accessSync(p);
    return true;
  } catch (e) {
    return false;
  }
}

const pages = ["index", "features", "settings"];

/* ---------------------------------------------------------- 1. 标签配对 */

// 这些是自闭合的标签，不需要 </...>
const VOID_TAGS = ["input", "image", "icon", "import", "include", "wxs"];

function checkTags(file) {
  const src = read(file);
  const stack = [];
  const re = /<(\/?)([a-zA-Z][\w-]*)([^>]*?)(\/?)>/g;
  let m;
  let bad = null;

  while ((m = re.exec(src))) {
    const closing = m[1] === "/";
    const tag = m[2];
    const selfClose = m[4] === "/";
    if (VOID_TAGS.indexOf(tag) >= 0 || selfClose) continue;

    if (closing) {
      if (!stack.length || stack[stack.length - 1] !== tag) {
        bad = "extra </" + tag + ">, top of stack: " + (stack[stack.length - 1] || "empty");
        break;
      }
      stack.pop();
    } else {
      stack.push(tag);
    }
  }

  ok(!bad && stack.length === 0, file + "  tag mismatch: " + (bad || "unclosed " + stack.join(",")));
}

/* ------------------------------------------------------ 2. 事件处理函数 */

function checkHandlers(page) {
  const wxml = path.join(ROOT, "pages", page, page + ".wxml");
  const js = path.join(ROOT, "pages", page, page + ".js");
  const src = read(wxml);
  const code = read(js);

  const fns = new Set();
  const re = /bind(?:tap|input|change|confirm|blur|focus)="(\w+)"/g;
  let m;
  while ((m = re.exec(src))) fns.add(m[1]);

  fns.forEach(function (fn) {
    const defined = new RegExp("\\b" + fn + "\\s*[:(]").test(code);
    ok(defined, page + ".wxml binds " + fn + " but " + page + ".js has no such method");
  });
  return fns.size;
}

/* ---------------------------------------------------------- 3. 图标类 */

function collectIcons(file, out) {
  const src = read(file);
  const re = /\bi-[a-z]+\b/g;
  let m;
  while ((m = re.exec(src))) out.add(m[0]);
}

/* ------------------------------------------------------ 4. 主题 CSS 变量 */

function cssBlock(css, selector) {
  const i = css.indexOf(selector);
  if (i < 0) return "";
  const start = css.indexOf("{", i);
  const end = css.indexOf("}", start);
  return css.slice(start + 1, end);
}

function varsIn(block) {
  const out = new Set();
  const re = /(--[\w-]+)\s*:/g;
  let m;
  while ((m = re.exec(block))) out.add(m[1]);
  return out;
}

/* ------------------------------------------------------------ 5. 工程结构 */

function checkProject() {
  const app = JSON.parse(read(path.join(ROOT, "app.json")));

  app.pages.forEach(function (p) {
    ["js", "json", "wxml", "wxss"].forEach(function (ext) {
      const f = path.join(ROOT, p + "." + ext);
      ok(exists(f), "app.json page missing file: " + p + "." + ext);
    });
  });

  const list = (app.tabBar && app.tabBar.list) || [];
  ok(list.length > 0, "app.json has no tabBar");
  list.forEach(function (it) {
    ["iconPath", "selectedIconPath"].forEach(function (k) {
      if (!it[k]) {
        ok(false, "tabBar item missing " + k + ": " + it.text);
        return;
      }
      ok(exists(path.join(ROOT, it[k])), "tabBar icon not found: " + it[k]);
    });
  });
}

/* ------------------------------------------------------------------ 执行 */

console.log("mini-program UI static check");

pages.forEach(function (p) {
  const wxml = path.join(ROOT, "pages", p, p + ".wxml");
  read(wxml);
});

console.log("  1. wxml tag balance");
pages.forEach(function (p) {
  checkTags(path.join(ROOT, "pages", p, p + ".wxml"));
});

console.log("  2. event handlers");
let handlerCount = 0;
pages.forEach(function (p) {
  handlerCount += checkHandlers(p);
});
ok(handlerCount > 0, "no bindtap found at all, wxml likely broken");

console.log("  3. icon classes");
const css = read(path.join(ROOT, "app.wxss"));
const definedIcons = new Set();
(function () {
  const re = /\.(i-[a-z]+)\s*\{/g;
  let m;
  while ((m = re.exec(css))) definedIcons.add(m[1]);
})();

const usedIcons = new Set();
pages.forEach(function (p) {
  collectIcons(path.join(ROOT, "pages", p, p + ".wxml"), usedIcons);
});

usedIcons.forEach(function (name) {
  ok(definedIcons.has(name), "icon ." + name + " used in wxml but not defined in app.wxss");
});
ok(definedIcons.size > 0, "no icon defined in app.wxss");

console.log("  4. theme variables");
const light = varsIn(cssBlock(css, ".theme-light"));
const dark = varsIn(cssBlock(css, ".theme-dark"));
ok(light.size > 0 && dark.size > 0, ".theme-light / .theme-dark blocks not found");

light.forEach(function (v) {
  ok(dark.has(v), "dark theme missing variable " + v);
});
dark.forEach(function (v) {
  ok(light.has(v), "light theme missing variable " + v);
});

const usedVars = new Set();
(function () {
  const re = /var\((--[\w-]+)\)/g;
  let m;
  while ((m = re.exec(css))) usedVars.add(m[1]);
})();
usedVars.forEach(function (v) {
  ok(light.has(v), "app.wxss uses undefined variable " + v);
});

console.log("  5. project structure");
checkProject();

console.log("");
if (failed === 0) {
  console.log("ALL PASS  (" + checks + " checks)");
  process.exit(0);
} else {
  console.log("FAILED  (" + failed + " / " + checks + ")");
  process.exit(1);
}
