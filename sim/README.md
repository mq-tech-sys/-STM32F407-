# 智能门锁 · Windows 虚拟设备

主工程里的 `sim/` 目录：一台跑在 Windows 上的**假门锁**，说的是和 STM32 固件**一模一样**的那套
ASCII 行协议（`unlock` / `fp_list` / `fp_del:2` …）。没有板子在身边时，可以拿小程序真刀真枪地
连它、开关它、删它的指纹和卡。

```
小程序  ──MQTT/WebSocket──►  巴法云  ──MQTT/TCP──►  本程序（虚拟门锁）
   └────────────── 或者 ──────────────►  ws://127.0.0.1:9001（本地模式，不联网）
```

它们为什么可信：`mocklock/lock.py` 是照着 `HARDWARE/net/remote_cmd.c` **一条一条抄下来的**，
命令、应答顺序、忙碌判断、开关锁后的 2 秒自动回锁都对齐；`sim/tests/test_protocol.py` 里的
32 条用例就是这套对照关系的守门人。所以"在模拟器上通了"≈"换成真板子也通"，
剩下真机才有的只是硬件本身（指纹头、读卡器、舵机）。

![控制台](console.png)

左：虚拟门锁　中：指纹库 / 卡库　下：报文区（蓝=收到、绿=发出、灰=内部事件）

---

## 运行前提：只要 Python 3，别的都不用装

整个模拟器只用 Python **标准库**（socket / ssl / threading / http / json …），
**没有任何第三方依赖，不需要 `pip install`，也不需要 Node、Java、数据库。**

| 你机器上的情况 | 双击 `run.bat` 会怎样 |
| --- | --- |
| 装了 Python 3，`python` 在 PATH 里 | 直接跑起来 |
| 装了但只有 `py` 启动器 | 脚本自动退到 `py -3`，照样跑 |
| 没装 Python | 窗口提示 `Python 3 not found. Install Python 3.8+ ...` 后退出 |

没装就去 <https://www.python.org/downloads/> 下 3.8 以上版本，**安装第一页务必勾上
"Add Python to PATH"**，否则双击还是找不到。

> 小程序那一侧仍然需要**微信开发者工具**——那是微信官方的工具，跟这台模拟器无关，绕不开。

---

## 快速开始

双击 **`sim/run.bat`**，会自动打开控制台 <http://127.0.0.1:8848>。

```
 主题       : lock000
 巴法云     : bemfa.com:9501  (ClientID = 设备私钥)
 本地 WS    : ws://127.0.0.1:9001
 控制台     : http://127.0.0.1:8848
```

启动后就有演示数据：指纹 `#1 #2 #5`、两张卡、开锁密码 `123456`，方便一进小程序就有东西可删。

> 只想照着点、不想读原理：看 **[TEST-STEPS.md](TEST-STEPS.md)**，从「第 0 步」按顺序做，
> 每一步都写了该看到什么。

---

## 两种测法

### 方法 A：走巴法云（和真机完全同一条链路，手机也能测）

模拟器用**设备私钥**登录巴法云，订阅 `lock000`、发布到 `lock000/set`，和固件一模一样。
小程序那边按下面填：

| 字段 | 填什么 |
|---|---|
| 服务器 | `wss://bemfa.com:9504/wss` |
| 主题名 | `lock000` |
| ClientID | `wxmp_lock01`（**不要填私钥**，否则会把设备顶下线） |
| 用户名 | 巴法云 **appID** |
| 密码 | 巴法云 **secretKey** |

appID / secretKey 在巴法云控制台「连接密钥」页面复制。

> 如果你巴法云里的主题不是 `lock000`（比如截图里是 `lock000` 之外的别的名字），
> 双击 **`run-mine.bat`**，它会依次问你主题名和该主题的私钥，用你账号里的那一对启动。

> ⚠️ 模拟器和真机用的是同一个 ClientID（私钥），**两者不能同时在线**，后连的会把先连的顶掉。
> 真机在旁边时，改 `--uid` 传一个自己的另一个私钥，或者干脆只跑本地模式。

### 方法 B：纯本地（不用网、不用巴法云，改完代码立刻能测）

1. 微信开发者工具 → 右上角「详情」→「本地设置」→ 勾选 **不校验合法域名、web-view…**
2. 小程序设置页：服务器改成 `ws://127.0.0.1:9001`，主题留 `lock000`，
   ClientID / 用户名 / 密码**随便填或留空**。
3. 点「保存并立即试连」。

这条路径走的是本程序内置的迷你 MQTT broker，一个字节都不出网。
**只适合开发者工具**：真机预览微信只允许 `wss://`，连不了 `ws://`。

---

## 一键诊断：`probe.bat`

链路不通时先跑它，它会把三件事分别告诉你：

```bat
probe.bat
probe.bat --appid 你的appID --secret 你的secretKey
```

```
[1/3] 设备侧  tcp://bemfa.com:9501  ClientID=私钥
      OK  CONNACK rc=0 + SUBACK rc=0，主题 lock000 可订阅、私钥有效
[2/3] 小程序侧  wss://bemfa.com:9504/wss  ClientID=wxmp_lock01
      OK  WSS + MQTT 握手成功，说明小程序这条链路本身没问题
[3/3] 主题路由：小程序发 ping → 设备回 pong
      OK  双向都通（183 ms）
```

**本机已经实测**：第 1 步通过（`CONNACK rc=0` + `SUBACK rc=0`）。
第 2、3 步需要你提供 appID / secretKey 才能跑——这也是唯一还没被验证过的一段。
如果之前"小程序连不上巴法云"，跑一下就能定位：

| 现象 | 原因 |
|---|---|
| `rc=4` / `rc=5` | appID 或 secretKey 填错了 |
| `SUBACK 0x80` | **主题名和私钥不是一对**（见下节，这是最常见的坑） |
| 超时 / 连不上 | 小程序后台没配 socket 合法域名，或本机网络出不去 9504 |
| `CONNACK 已收到、SUBACK 没等到` | 同一个私钥重复登录被顶号了，先关掉模拟器/真机 |

### ⚠️ 主题名和私钥是一对一的

巴法云里**每个主题有自己的私钥**，用 A 主题的私钥去订阅 B 主题，连接照样成功
（`CONNACK rc=0`），但订阅会被拒绝，`SUBACK` 返回 **0x80**，然后一条消息都收不到。

2026-10-19 实测（同一串私钥作为 ClientID）：

```
订阅 lock001 -> SUBACK return code = 0x80   ← 拒绝
订阅 lock000 -> SUBACK return code = 0x00   ← 成功
```

所以三处主题名必须完全一致：`HARDWARE/net/bemfa_mqtt.h` 的 `BAFA_TOPIC_LOCK`、
小程序设置页的主题名、巴法云控制台里那个主题。改主题不能只改一边。

---

## 控制台能做什么

| 区域 | 作用 |
|---|---|
| 门锁 | 开锁 / 上锁，等同小程序点按钮 |
| 指纹 | 看列表、删单条（`fp_del:<id>`）、模拟录入、清空 |
| RFID 卡 | 看列表、删单条（`card_del:<idx>`）、模拟刷卡、清空 |
| 报文 | 每一条收发都记下来，蓝=收到、绿=发出；底部输入框可以直接发命令 |

页面上点按钮和从小程序发命令走的是**同一个** `handle(line)` 入口，
所以"控制台能跑通"基本等价于"小程序能跑通"，剩下的只是链路问题——那正好是 `probe.bat` 管的。

---

## 协议对照（和 `HARDWARE/net/remote_cmd.c` 一致）

| 小程序 → 设备 | 设备 → 小程序 |
|---|---|
| `unlock` / `close` | `ack=unlock` / `ack=close`，随后 `state=unlock` / `state=lock` |
| `state` | `state=lock` / `state=unlock` |
| `fp_add` | `ack=fp_add`，约 2 秒后 `ack=fp_enrolled` + `num=fp,n` |
| `fp_list` | 逐行 `fp=<id>`，最后 `fpend=<数量>` |
| `fp_del:<id>` | `ack=fp_del_ok` / `ack=fp_del_fail` / `ack=fp_del_bad` |
| `fp_del` | `ack=fp_del`（清空） |
| `card_list` | 逐行 `card=<索引>,<8位十六进制UID>`，最后 `cardend=<数量>` |
| `card_del:<索引>` | `ack=card_del_ok` / `ack=card_del_fail` / `ack=card_del_bad` |
| `card_add` | `ack=card_add`，约 2 秒后 `ack=card_enrolled` + `num=card,n` |
| `pwd:123456` | `ack=pwd_ok` + `state=unlock`，或 `ack=pwd_fail` |
| `pwd_set:<6位>` | `ack=pwd_set_ok` / `ack=pwd_set_bad` |
| `ping` | `pong` |
| 设备忙（正在录入/刷卡）时发列表命令 | `err=busy` |

---

## 和真机的差别

只有三处，都不影响小程序适配：

1. **录入是自动的**：`fp_add` / `card_add` 后等 2 秒，程序自己塞一条新记录并补发
   `num=fp,n` / `num=card,n`。真机要你在 OLED 提示下按手指 / 贴卡，且不会主动补这两条。
   小程序对不认识的响应是"原样显示"，不会被带偏。
2. **不会掉电重启**：真机重启后指纹库还在、状态回到上锁；模拟器把这部分存进了
   `sim_state.json`，重启还在，只有 `--reset` 或控制台按钮会恢复演示数据。
3. **容量**：指纹按 100、卡片按 10（对齐 `FP_CAPACITY` 和 `g_card_list`）。

---

## 常用参数

```bat
python -m mocklock.app --mode local          :: 只跑本地 WS，不连巴法云
python -m mocklock.app --reset               :: 启动前恢复演示数据
python -m mocklock.app --topic lock002       :: 换主题（小程序里要同步改）
python -m mocklock.app --probe --appid xx --secret yy
python -m mocklock.app --help
```

---

## 自测

```bat
python tests\test_protocol.py
```

32 条用例，覆盖开关锁顺序、指纹/卡列表格式、单项删除的 ok/fail/bad 三种应答、
删除后索引前移、密码校验、忙碌时 `err=busy`、状态持久化、开锁后 2 秒自动回锁
（`unlock` 和 `pwd:` 两条路径都要回锁）、以及"回投的应答不能当真命令再回一次"。
另有两条真正的端到端用例：两个 WebSocket 客户端经内置 broker 互发，等价于小程序 ↔ 设备。
当前结果：`Ran 32 tests ... OK`。

---

## 文件结构

```
mocklock/
  mqtt.py         MQTT 3.1.1 客户端（TCP / TLS / WebSocket），零第三方依赖
  lock.py         虚拟门锁：逐条对齐 remote_cmd.c 的命令与应答
  localbroker.py  内置迷你 broker（MQTT over WebSocket），给开发者工具直连用
  web.py          本地控制台 + SSE 推送
  app.py          入口：装配云端/本地两条链路、诊断模式
tests/test_protocol.py
run.bat  run-local.bat  run-mine.bat  probe.bat
```

只用了 Python 标准库，不需要 `pip install` 任何东西。
