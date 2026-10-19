/**
 * 敏感信息扫描：防止巴法云 AppID / SecretKey 之类的东西被误提交。
 *
 * 背景：这些值曾经被写进截图渲染脚本，结果出现在 README 的截图里。
 * 截图和文档里只允许出现占位符（beid_your_appid_here / your_secret_key_here）。
 *
 * 只扫 git 已跟踪的文本文件，二进制（图片）跳过——图片靠肉眼和
 * 渲染脚本用占位符来保证。
 */

const { execFileSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..");
const TEXT_EXT = new Set([
  ".js", ".json", ".md", ".txt", ".c", ".h", ".py", ".bat", ".wxml", ".wxss", ".yml", ".yaml", ".html"
]);

// 巴法云 AppID 是 beid_ + 一长串随机字符；SecretKey 是 32 位随机字母数字。
const RULES = [
  {
    name: "巴法云 AppID",
    // beid_ 后面跟 >=20 个连续字母数字才像真的；占位符带下划线，不会命中
    re: /beid_[A-Za-z0-9]{20,}/g
  },
  {
    name: "疑似 SecretKey 赋值",
    // 允许 `SecretKey  xxx`、`secretKey: "xxx"`、`secret_key = 'xxx'` 几种写法
    re: /(?:secretkey|secret_key)\s*[:=]?\s*["'`]?([A-Za-z0-9]{24,})["'`]?/gi
  }
];

let files = [];
try {
  files = execFileSync("git", ["ls-files"], { cwd: ROOT, encoding: "utf8" })
    .split("\n").map((s) => s.trim()).filter(Boolean);
} catch (e) {
  console.log("  ! 不是 git 仓库，跳过敏感信息扫描");
  process.exit(0);
}

const hits = [];

for (const rel of files) {
  if (!TEXT_EXT.has(path.extname(rel).toLowerCase())) continue;
  const abs = path.join(ROOT, rel);
  if (!fs.existsSync(abs)) continue;

  let text;
  try {
    text = fs.readFileSync(abs, "utf8");
  } catch (e) {
    continue; // 非 UTF-8（比如 GBK 源码），跳过
  }
  if (text.charCodeAt(0) === 0xfeff) text = text.slice(1);

  const lines = text.split(/\r?\n/);
  lines.forEach((line, i) => {
    for (const rule of RULES) {
      rule.re.lastIndex = 0;
      let m;
      while ((m = rule.re.exec(line)) !== null) {
        const value = m[1] || m[0];
        hits.push({ file: rel, line: i + 1, rule: rule.name, value });
      }
    }
  });
}

if (hits.length === 0) {
  console.log(`  扫描 ${files.length} 个已跟踪文件，未发现密钥`);
  process.exit(0);
}

console.log("");
console.log(`  ✗ 发现 ${hits.length} 处疑似密钥：`);
for (const h of hits) {
  const masked = h.value.slice(0, 8) + "..." + h.value.slice(-4);
  console.log(`    ${h.file}:${h.line}  [${h.rule}]  ${masked}`);
}
console.log("");
console.log("  处理办法：改成占位符（beid_your_appid_here / your_secret_key_here），");
console.log("  真实凭据只写在 sim/CONFIG-MINE.md（已被 .gitignore 忽略）。");
process.exit(1);
