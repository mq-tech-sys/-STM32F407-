"""
mocklock.web —— 本地控制台（http://127.0.0.1:8848）

一块屏看两件事：
  1) 虚拟门锁现在的样子（锁/指纹/卡/密码/忙碌状态）
  2) 小程序和它之间的每一条报文（收发都记）
页面上的按钮直接走和 MQTT 完全相同的 handle(line) 入口，所以在这里能做的，
在小程序里同样能做。
"""

import json
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def port_in_use(host, port):
    """端口上已经有东西在监听就返回 True（Windows 的 SO_REUSEADDR 会让两个进程抢同一个端口，
    必须主动探测，否则两个模拟器会同时连巴法云、互收对方的回包，打成死循环）。"""
    s = socket.socket()
    s.settimeout(0.4)
    try:
        s.connect((host, port))
        return True
    except OSError:
        return False
    finally:
        s.close()


PAGE = r"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>虚拟门锁 · 控制台</title>
<style>
:root{
  --bg:#f4f6f9; --card:#fff; --line:#e6e9ef; --ink:#111827; --ink2:#6b7280;
  --brand:#2f6fed; --brand-soft:#eef3ff; --ok:#16a34a; --ok-soft:#eaf7ee;
  --bad:#dc2626; --bad-soft:#fdeeee; --warn:#d97706;
  --radius:16px; --shadow:0 1px 2px rgba(16,24,40,.04),0 8px 24px -12px rgba(16,24,40,.12);
}
*{box-sizing:border-box}
html,body{margin:0}
body{
  background:var(--bg); color:var(--ink);
  font:14px/1.55 -apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;
  -webkit-font-smoothing:antialiased;
}
.wrap{max-width:1180px;margin:0 auto;padding:26px 22px 40px}
header{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap;margin-bottom:20px}
.brand{display:flex;align-items:center;gap:12px}
.logo{width:42px;height:42px;border-radius:12px;background:var(--brand-soft);display:grid;place-items:center;color:var(--brand)}
h1{font-size:19px;margin:0;letter-spacing:.2px}
.sub{color:var(--ink2);font-size:12.5px;margin-top:2px}
.badges{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.badge{display:inline-flex;align-items:center;gap:6px;padding:6px 11px;border-radius:999px;
  background:#fff;border:1px solid var(--line);font-size:12.5px;color:var(--ink2)}
.badge i{width:7px;height:7px;border-radius:50%;background:#c9ced8;display:inline-block}
.badge.on i{background:var(--ok);box-shadow:0 0 0 3px var(--ok-soft)}
.badge.err i{background:var(--bad);box-shadow:0 0 0 3px var(--bad-soft)}

.grid{display:grid;grid-template-columns:340px 1fr 1fr;gap:16px;align-items:start}
@media(max-width:980px){.grid{grid-template-columns:1fr 1fr}}
@media(max-width:680px){.grid{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--shadow);overflow:hidden}
.card-hd{display:flex;align-items:center;justify-content:space-between;padding:15px 18px;border-bottom:1px solid var(--line)}
.card-t{font-weight:600;font-size:14px}
.card-d{color:var(--ink2);font-size:12px;margin-top:1px}
.pill{font-size:12px;color:var(--ink2);background:#f7f8fa;border:1px solid var(--line);border-radius:999px;padding:2px 9px}
.card-bd{padding:16px 18px}

/* 锁 */
.lock-wrap{display:flex;flex-direction:column;align-items:center;padding:26px 18px 20px;gap:14px}
.dial{width:132px;height:132px;border-radius:50%;display:grid;place-items:center;background:var(--ok-soft);transition:.25s}
.dial.open{background:var(--brand-soft)}
.dial svg{width:64px;height:64px}
.state{font-size:17px;font-weight:600}
.state.open{color:var(--brand)}
.state.lock{color:var(--ink)}
.hint{color:var(--ink2);font-size:12.5px;text-align:center;min-height:18px}
.btn-row{display:flex;gap:10px;width:100%}
.btn{flex:1;border:1px solid var(--line);background:#fff;color:var(--ink);border-radius:11px;padding:10px 12px;
  font-size:13.5px;cursor:pointer;transition:.15s;font-family:inherit}
.btn:hover{border-color:#cfd6e4;background:#fafbfc}
.btn.primary{background:var(--brand);border-color:var(--brand);color:#fff}
.btn.primary:hover{background:#255ed6}
.btn.ghost{flex:0 0 auto;padding:6px 11px;font-size:12.5px;color:var(--ink2)}
.btn.danger:hover{border-color:#f3c1c1;color:var(--bad);background:var(--bad-soft)}

/* 列表 */
.chips{display:flex;flex-wrap:wrap;gap:8px;min-height:36px}
.chip{display:inline-flex;align-items:center;gap:7px;background:#f7f8fa;border:1px solid var(--line);
  border-radius:9px;padding:6px 9px;font-size:12.5px}
.chip b{font-weight:600;color:var(--ink)}
.chip code{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;color:var(--ink2)}
.chip .x{border:0;background:transparent;color:#9aa2b1;cursor:pointer;font-size:14px;line-height:1;padding:0 1px;font-family:inherit}
.chip .x:hover{color:var(--bad)}
.empty{color:#9aa2b1;font-size:12.5px;padding:8px 0}
.tools{display:flex;gap:8px;margin-top:14px;flex-wrap:wrap}

/* 日志 */
.log-bd{padding:0}
#log{height:250px;overflow:auto;padding:12px 16px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12.2px}
.ln{display:flex;gap:9px;padding:2.5px 0;word-break:break-all}
.ln .t{color:#b0b7c3;flex:0 0 auto}
.ln.rx .m{color:#1f4fd0}
.ln.tx .m{color:#177b45}
.ln.info .m{color:#6b7280}
.ln.warn .m,.ln.error .m{color:#b91c1c}
.cmdbar{display:flex;gap:8px;padding:12px 16px;border-top:1px solid var(--line);background:#fbfcfd}
.cmdbar input{flex:1;border:1px solid var(--line);border-radius:10px;padding:8px 11px;font-size:13px;font-family:inherit;outline:none}
.cmdbar input:focus{border-color:var(--brand);box-shadow:0 0 0 3px var(--brand-soft)}
.cmdbar button{border:1px solid var(--brand);background:var(--brand);color:#fff;border-radius:10px;padding:8px 14px;font-size:13px;cursor:pointer;font-family:inherit}

footer{margin-top:18px;color:var(--ink2);font-size:12.5px;line-height:1.8}
footer code{background:#fff;border:1px solid var(--line);border-radius:6px;padding:1px 6px;font-size:12px}
.tip{background:#fff;border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-top:12px}
.tip b{color:var(--ink)}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="brand">
      <div class="logo">
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round">
          <rect x="4" y="10" width="16" height="10" rx="2.4"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/><circle cx="12" cy="15" r="1.2" fill="currentColor" stroke="none"/>
        </svg>
      </div>
      <div>
        <h1>虚拟门锁 · 控制台</h1>
        <div class="sub">STM32 智能门锁项目的 Windows 模拟设备端</div>
      </div>
    </div>
    <div class="badges">
      <span class="badge" id="bCloud"><i></i><span>巴法云</span></span>
      <span class="badge" id="bLocal"><i></i><span>本地 WS</span></span>
      <span class="badge" id="bAct"><i></i><span>暂无活动</span></span>
    </div>
  </header>

  <div class="grid">
    <section class="card">
      <div class="card-hd">
        <div><div class="card-t">门锁</div><div class="card-d">模拟舵机，无硬件</div></div>
        <span class="pill" id="busyPill">空闲</span>
      </div>
      <div class="lock-wrap">
        <div class="dial" id="dial">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round">
            <rect x="3.5" y="10.5" width="17" height="10.5" rx="2.6" id="body"/>
            <path d="M7.5 10.5V7a4.5 4.5 0 0 1 9 0v3.5" id="shackle" style="transition:.3s;transform-origin:12px 10px"/>
            <circle cx="12" cy="15.6" r="1.25" fill="currentColor" stroke="none"/>
          </svg>
        </div>
        <div class="state lock" id="stateText">已上锁</div>
        <div class="hint" id="stateSub">等待命令</div>
        <div class="btn-row">
          <button class="btn primary" onclick="send('unlock')">开锁</button>
          <button class="btn" onclick="send('close')">上锁</button>
        </div>
      </div>
    </section>

    <section class="card">
      <div class="card-hd">
        <div><div class="card-t">指纹</div><div class="card-d">fp_list / fp_del:&lt;id&gt;</div></div>
        <span class="pill" id="fpCount">0</span>
      </div>
      <div class="card-bd">
        <div class="chips" id="fpList"></div>
        <div class="tools">
          <button class="btn ghost" onclick="send('fp_add')">模拟录入</button>
          <button class="btn ghost" onclick="send('fp_list')">读取列表</button>
          <button class="btn ghost danger" onclick="send('fp_del')">清空</button>
        </div>
      </div>
    </section>

    <section class="card">
      <div class="card-hd">
        <div><div class="card-t">RFID 卡</div><div class="card-d">card_list / card_del:&lt;idx&gt;</div></div>
        <span class="pill" id="cardCount">0</span>
      </div>
      <div class="card-bd">
        <div class="chips" id="cardList"></div>
        <div class="tools">
          <button class="btn ghost" onclick="send('card_add')">模拟刷卡</button>
          <button class="btn ghost" onclick="send('card_list')">读取列表</button>
          <button class="btn ghost danger" onclick="send('card_del')">清空</button>
        </div>
      </div>
    </section>

    <section class="card" style="grid-column:1/-1">
      <div class="card-hd">
        <div><div class="card-t">报文</div><div class="card-d">蓝 = 收到 / 绿 = 发出 / 灰 = 内部事件</div></div>
        <button class="btn ghost" onclick="document.getElementById('log').innerHTML='';lastId=0">清屏</button>
      </div>
      <div class="card-bd log-bd">
        <div id="log"></div>
        <div class="cmdbar">
          <input id="cmd" placeholder="直接发一条命令，如 fp_list、fp_del:2、pwd:123456" onkeydown="if(event.key==='Enter')sendCmd()">
          <button onclick="sendCmd()">发送</button>
        </div>
      </div>
    </section>
  </div>

  <footer>
    <div class="tip">
      <b>小程序怎么连这个模拟器</b><br>
      · 云端模式：设置页服务器填 <code>wss://bemfa.com:9504/wss</code>，主题 <code>__TOPIC__</code>，
      用户名填巴法云 appID、密码填 secretKey。设备端（本程序）已用私钥登录巴法云。<br>
      · 本地模式：开发者工具勾选「不校验合法域名」，服务器改成 <code>__LOCAL_URL__</code>，其余留空即可。
    </div>
    <button class="btn ghost" style="margin-top:12px" onclick="resetDemo()">恢复演示数据（指纹 #1 #2 #5 / 2 张卡 / 密码 123456）</button>
  </footer>
</div>

<script>
let lastId = 0;

function esc(s){
  return String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
}

function renderLogs(logs){
  const box = document.getElementById('log');
  for(const l of logs){
    if(l.id <= lastId) continue;
    lastId = l.id;
    const d = document.createElement('div');
    d.className = 'ln ' + l.kind;
    d.innerHTML = '<span class="t">' + l.ts + '</span><span class="m">' + esc(l.text) + '</span>';
    box.appendChild(d);
  }
  while(box.children.length > 400) box.removeChild(box.firstChild);
  box.scrollTop = box.scrollHeight;
}

function renderChips(id, items, delFn){
  const box = document.getElementById(id);
  if(!items.length){ box.innerHTML = '<div class="empty">（空）</div>'; return; }
  box.innerHTML = '';
  items.forEach(it => {
    const d = document.createElement('div');
    d.className = 'chip';
    d.innerHTML = '<b>#' + it.idx + '</b>' + (it.uid ? '<code>' + it.uid + '</code>' : '') +
                  '<button class="x" title="删除">×</button>';
    d.querySelector('.x').onclick = () => delFn(it.idx);
    box.appendChild(d);
  });
}

function render(s){
  const dial = document.getElementById('dial');
  const st = document.getElementById('stateText');
  if(s.locked){
    dial.classList.remove('open');
    st.textContent = '已上锁';
    st.className = 'state lock';
    document.getElementById('shackle').style.transform = 'rotate(0deg)';
  }else{
    dial.classList.add('open');
    st.textContent = '已开锁';
    st.className = 'state open';
    document.getElementById('shackle').style.transform = 'rotate(-22deg) translateX(2px)';
  }

  document.getElementById('fpCount').textContent = s.fps.length + ' 条';
  document.getElementById('cardCount').textContent = s.cards.length + ' 张';
  renderChips('fpList', s.fps.map(i => ({idx:i})), i => send('fp_del:' + i));
  renderChips('cardList', s.cards, i => send('card_del:' + i));

  const busy = document.getElementById('busyPill');
  busy.textContent = s.busy ? (s.busy_what || '忙碌') : '空闲';
  busy.style.color = s.busy ? '#d97706' : '';

  const c = s.cloud;
  const bc = document.getElementById('bCloud');
  if(c){
    bc.className = 'badge ' + (c.connected ? 'on' : (c.error ? 'err' : ''));
    bc.lastElementChild.textContent = c.connected ? '巴法云已连' : (c.error ? '巴法云未连' : '巴法云连接中');
  }else{
    bc.className = 'badge';
    bc.lastElementChild.textContent = '巴法云未启用';
  }

  const bl = document.getElementById('bLocal');
  if(s.local_enabled === false){
    bl.className = 'badge';
    bl.lastElementChild.textContent = '本地 WS 未启用';
  }else{
    bl.className = 'badge ' + (s.local_clients > 0 ? 'on' : '');
    bl.lastElementChild.textContent = '本地 WS ' + (s.local_clients > 0 ? s.local_clients + ' 个' : '待连');
  }

  const ba = document.getElementById('bAct');
  if(s.last_rx){
    const ago = Math.max(0, Math.round(Date.now()/1000 - s.last_rx));
    ba.className = 'badge on';
    ba.lastElementChild.textContent = '小程序 ' + ago + ' 秒前活跃';
  }else{
    ba.className = 'badge';
    ba.lastElementChild.textContent = '还没有收到命令';
  }
  renderLogs(s.logs || []);
}

function send(line){
  fetch('/api/cmd', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({line: line})
  });
}

function sendCmd(){
  const el = document.getElementById('cmd');
  const v = el.value.trim();
  if(!v) return;
  send(v);
  el.value = '';
}

function resetDemo(){
  if(!confirm('恢复演示数据？当前的指纹/卡改动会丢失。')) return;
  fetch('/api/reset', {method:'POST'}).then(() => { lastId = 0;
    document.getElementById('log').innerHTML = ''; });
}

const es = new EventSource('/api/stream');
es.onmessage = e => { try{ render(JSON.parse(e.data)); }catch(err){} };
</script>
</body>
</html>
"""


class Hub:
    """状态 + 日志的共享容器，HTTP 线程和 MQTT 线程都往这里写。"""

    def __init__(self, lock, cloud_info=None, local_info=None, topic="lock000",
                 local_url="", local_enabled=True):
        self.lock = lock
        self.cloud_info = cloud_info or (lambda: {})
        self.local_info = local_info or (lambda: {})
        self.topic = topic
        self.local_url = local_url
        self.local_enabled = local_enabled
        self._logs = []
        self._seq = 0
        self._mutex = threading.Lock()

    def add_log(self, kind, text):
        with self._mutex:
            self._seq += 1
            self._logs.append({
                "id": self._seq,
                "kind": kind,
                "text": text,
                "ts": time.strftime("%H:%M:%S"),
            })
            if len(self._logs) > 400:
                del self._logs[:200]

    def state(self):
        s = self.lock.snapshot()
        s["cloud"] = self.cloud_info()
        s["topic"] = self.topic
        s["local_enabled"] = self.local_enabled
        s["local_clients"] = (self.local_info() or {}).get("clients", 0)
        with self._mutex:
            s["logs"] = list(self._logs[-80:])
        return s


def _make_handler(hub):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        # -------------------------------------------------- 工具
        def _json(self, obj, code=200):
            body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _html(self):
            page = (PAGE
                    .replace("__TOPIC__", hub.topic)
                    .replace("__LOCAL_URL__", hub.local_url or "ws://127.0.0.1:9001"))
            body = page.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        # -------------------------------------------------- GET
        def do_GET(self):
            if self.path in ("/", "/index.html"):
                self._html()
                return
            if self.path == "/api/state":
                self._json(hub.state())
                return
            if self.path == "/api/stream":
                self._stream()
                return
            self._json({"error": "not found"}, 404)

        def _stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream; charset=utf-8")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                while True:
                    payload = json.dumps(hub.state(), ensure_ascii=False)
                    self.wfile.write(("data: " + payload + "\n\n").encode("utf-8"))
                    self.wfile.flush()
                    time.sleep(0.7)
            except (BrokenPipeError, ConnectionResetError, OSError):
                return

        # -------------------------------------------------- POST
        def do_POST(self):
            try:
                n = int(self.headers.get("Content-Length") or 0)
                raw = self.rfile.read(n) if n else b"{}"
                data = json.loads(raw.decode("utf-8") or "{}")
            except (ValueError, UnicodeDecodeError):
                self._json({"ok": False, "error": "bad json"}, 400)
                return

            if self.path == "/api/cmd":
                line = str(data.get("line", "")).strip()
                if not line:
                    self._json({"ok": False, "error": "empty"}, 400)
                    return
                hub.lock.handle(line, from_console=True)
                self._json({"ok": True})
                return

            if self.path == "/api/reset":
                hub.lock.reset()
                self._json({"ok": True})
                return

            self._json({"ok": False, "error": "not found"}, 404)

    return Handler


class Console:
    def __init__(self, hub, host="127.0.0.1", port=8848):
        self.hub = hub
        self.host = host
        self.port = port
        self._server = None

    def start(self):
        if port_in_use(self.host, self.port):
            raise SystemExit(
                "\n[错误] 控制台端口 %d 已被占用 —— 已经有一个模拟器在跑了。\n"
                "       两个模拟器同时连巴法云会互收对方的回包，刷出大量 err=unknown。\n"
                "       先关掉旧的那个（任务管理器里结束多余 python 进程），再重新启动。\n" % self.port
            )
        try:
            self._server = ThreadingHTTPServer((self.host, self.port), _make_handler(self.hub))
        except OSError as e:
            raise SystemExit("\n[错误] 控制台端口 %d 绑定失败：%s\n" % (self.port, e))
        self._server.daemon_threads = True
        threading.Thread(target=self._server.serve_forever, name="console", daemon=True).start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
