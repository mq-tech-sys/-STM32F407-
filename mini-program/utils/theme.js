/**
 * utils/theme.js
 * 明暗主题：读写 + 把系统级 UI（导航栏 / 页面底色 / tabBar）一起换掉。
 *
 * 页面用法：
 *   const theme = require("../../utils/theme.js");
 *   onShow() { this.setData({ theme: theme.sync() }); }   // sync 返回当前主题并刷系统 UI
 *   onToggleTheme() { this.setData({ theme: theme.toggle() }); }
 *
 * 页面根节点写 class="app theme-{{theme}}"，配色全部走 app.wxss 里的 CSS 变量。
 */

const KEY = "smart_lock_theme";

const CHROME = {
  light: {
    nav: "#ffffff",
    front: "#000000",
    page: "#f4f6fb",
    tabBg: "#ffffff",
    tabColor: "#8a94a6",
    tabActive: "#2563eb"
  },
  dark: {
    nav: "#11151d",
    front: "#ffffff",
    page: "#0b0e14",
    tabBg: "#11151d",
    tabColor: "#7c8698",
    tabActive: "#6aa1ff"
  }
};

function get() {
  try {
    return wx.getStorageSync(KEY) === "dark" ? "dark" : "light";
  } catch (e) {
    return "light";
  }
}

function set(t) {
  const v = t === "dark" ? "dark" : "light";
  try {
    wx.setStorageSync(KEY, v);
  } catch (e) {}
  chrome(v);
  return v;
}

function toggle() {
  return set(get() === "dark" ? "light" : "dark");
}

/** 让系统级 UI（导航栏、下拉背景、tabBar）跟上主题 */
function chrome(t) {
  const c = CHROME[t] || CHROME.light;

  wx.setNavigationBarColor({
    frontColor: c.front,
    backgroundColor: c.nav,
    fail: function () {}
  });

  wx.setBackgroundColor({
    backgroundColor: c.page,
    backgroundColorTop: c.page,
    backgroundColorBottom: c.page,
    fail: function () {}
  });

  wx.setTabBarStyle({
    color: c.tabColor,
    selectedColor: c.tabActive,
    backgroundColor: c.tabBg,
    borderStyle: t === "dark" ? "black" : "white",
    fail: function () {}
  });
}

/** 页面进入时调用：返回当前主题，同时把系统 UI 对齐（防止从别的页面切过来后不一致） */
function sync() {
  const t = get();
  chrome(t);
  return t;
}

module.exports = { KEY, get, set, toggle, sync, chrome };
