# 基于 STM32F407 的智能指纹门锁

> 一块 F407 + 一颗 AS608 指纹头 + 一张 RC522 读卡器，做出**指纹 / 密码 / 刷卡 / WiFi / 蓝牙**五种开锁方式的成品级门锁固件。
> 基于 **FreeRTOS** 多任务架构，标准库（SPL）开发，Keil MDK5 一键编译烧录。
> 配套**微信小程序**，可以远程开关门、远程录入指纹、远程添加/删除卡片。

---

## 一、功能一览

| 开锁方式 | 说明 |
| --- | --- |
| 指纹 | AS608 指纹头，UART 通信；支持录入 / 匹配 / 清空指纹库，匹配带得分显示 |
| 密码 | 4×4 矩阵键盘输入 6 位数字密码，OLED 上以 `*` 掩码回显，支持修改密码 |
| RFID 刷卡 | MFRC522 读头，最多登记 10 张卡；**自动轮询刷卡**，未授权卡蜂鸣告警 |
| WiFi 远程 | ESP8266 连**巴法云 MQTT**，小程序下发命令开锁，门锁状态实时回传 |
| 蓝牙远程 | BLE 串口透传模块，手机在门锁附近（约 10 米）直连开锁，不依赖外网 |

- 0.96" OLED 实时显示锁 / 解锁图标与全部中文提示
- 舵机开锁后 **2 秒自动回锁**，状态变化同步推给小程序
- 蜂鸣器操作提示音；串口 115200 输出完整调试日志
- 小程序操作：**一键开锁 / 上锁 / 查询状态 / 录入指纹 / 删除指纹 / 添加卡 / 删卡 / 密码开锁 / 改密码**

## 二、技术栈

| 项目 | 说明 |
| --- | --- |
| MCU | STM32F407ZET6（168 MHz，512K Flash / 192K RAM，LQFP144） |
| 库 | ST 标准外设库 SPL v1.4.0（**非** HAL/LL），`USE_STDPERIPH_DRIVER` |
| RTOS | FreeRTOS 10.x，抢占式调度，`configTICK_RATE_HZ = 1000`，heap_4，堆 75 KB |
| 工具链 | Keil MDK 5 + ARMCC（AC5），工程文件 `USER/FreeRTOS.uvprojx`，**已开启 C99** |
| 云平台 | 巴法云 MQTT 3.1.1（`bemfa.com:9501`），STM32 侧自实现 MQTT 报文，ESP8266 只用 AT 透传 |
| 小程序 | 原生微信小程序，无第三方依赖；MQTT over WebSocket 与 BLE 各一套客户端 |

## 三、硬件接线

| 模块 | 接口 | 引脚 | 备注 |
| --- | --- | --- | --- |
| AS608 指纹头 | USART2 | PA2 / PA3 | 57600 bps |
| ESP8266 / BLE 模块 | USART3 | PB10 / PB11 | **二选一**，见第 8.3 节 |
| MFRC522 读卡器 | 软件 SPI | SCK `PD7`、MOSI `PC6`、MISO `PC8`、CS `PD6`、RST `PC11` | 非硬件 SPI |
| OLED 0.96" (SSD1306) | 软件 I2C | SCL `PB15`、SDA `PD10` | 128×64 |
| SG90 舵机 | TIM4_CH3 PWM | `PD14` | 50 Hz，0.5 ms=关锁 / 1.5 ms=开锁 |
| 蜂鸣器 | GPIO | `PF8` | 高电平响 |
| 独立按键 ×3 | GPIO | KEY1 `PA0`、KEY2 `PE2`、KEY3 `PE3` | 低电平有效 |
| 4×4 矩阵键盘 | GPIO 扫描 | 行：`PB7` `PA4` `PG15` `PC7`<br>列：`PC9` `PB6` `PE6` `PA8` | 行列扫描 + 20 ms 消抖 |
| 调试串口 | USART1 | PA9 / PA10 | 115200 bps，`printf` 输出 |

> 接线定义集中在各模块头文件里（`oled.h`、`beep.h`、`key.h`、`MFRC522.h` 等），换板子只改这几个宏。

## 四、本机操作说明

### 独立按键

| 按键 | 短按 | 双击 | 长按 |
| --- | --- | --- | --- |
| KEY1 | 录入指纹 | 添加 RFID 卡 | 密码开锁 |
| KEY2 | 刷指纹 | 删除**全部** RFID 卡 | 修改密码 |
| KEY3 | 删除**全部**指纹 | — | — |

### 矩阵键盘

| 按键 | 作用 |
| --- | --- |
| `0`–`9` | 输入密码（最多 6 位，屏幕掩码显示） |
| `*` | 退格删除一位 |
| `#` | 确认提交 |
| `D` | 退出键盘测试模式 |

## 五、远程控制命令表

**手机与设备之间就是一条条 ASCII 行文本**，WiFi(MQTT) 与蓝牙(BLE) 两条链路共用同一份解析代码（`HARDWARE/net/remote_cmd.c`）。远程命令最终都转换成"等同按了某个键"的事件，走完全相同的业务路径。

### 手机 → 设备

| 命令 | 作用 |
| --- | --- |
| `unlock` | 开锁 |
| `close` | 立即上锁 |
| `state` | 查询当前门锁状态 |
| `fp_add` | 进入**录入指纹**流程（人需在门锁旁连按两次同一手指） |
| `fp_del` | 清空设备上全部指纹 |
| `fp_num` | 查询已录指纹数量 |
| `card_add` | 进入**添加 RFID 卡**流程（人需把卡贴上读卡器） |
| `card_del` | 清空全部已登记卡 |
| `card_num` | 查询已登记卡数量 |
| `pwd:123456` | 用密码开锁（密码存在设备里，直接比对） |
| `pwd_set:654321` | 远程修改开锁密码 |
| `ping` / `help` | 链路探测 / 命令列表 |

### 设备 → 手机

| 上报 | 含义 |
| --- | --- |
| `state=lock` / `state=unlock` | 门锁已上锁 / 已开锁（状态变化时主动推） |
| `ack=unlock` `ack=close` `ack=fp_add` `ack=fp_del` `ack=card_add` `ack=card_del` | 命令已受理 |
| `ack=pwd_ok` / `ack=pwd_fail` / `ack=pwd_set_ok` / `ack=pwd_set_bad` | 密码校验 / 改密结果 |
| `num=fp,<n>` / `num=card,<n>` | 指纹 / 卡片数量 |
| `pong` / `err=unknown` / `help=<列表>` | 探测应答 / 未知命令 |

## 六、软件架构

FreeRTOS 多任务，**一个硬件模块一个任务**，任务间用事件标志组 / 消息队列 / 互斥量解耦：

| 任务 | 栈 | 职责 |
| --- | --- | --- |
| `task_init` | 512 | 建队列/锁/事件组，初始化全部外设，拉起其余任务后自删 |
| `task_oled` | 512 | 检测 `g_lock_state` 变化，刷新锁 / 解锁图标 |
| `task_servo` | 512 | 从 `g_servo_queue` 取指令驱动舵机，2 秒自动回锁并上报状态 |
| `task_as608` | 512 | 短按事件 → 录入 / 匹配 / 清空指纹 |
| `task_keyboard` | 512 | 长按事件 → 密码开锁 / 修改密码 |
| `task_rfid` | 512 | 双击事件 → 添加卡 / 清空卡列表 |
| `task_rfid_unlock` | 512 | 100 ms 轮询刷卡，命中已登记卡即开锁 |
| `task_key` | 512 | 10 ms 扫描 3 个独立按键，产生短按/长按/双击事件 |
| `task_esp8266` | 512 | 10 ms 推进**巴法云 MQTT 状态机**（联网 / 订阅 / 心跳 / 重连） |
| `task_ble24` | 512 | 10 ms 收蓝牙字节，按行切分后交给命令层 |

**并发模型**

```
按键任务 ──set bits──▶ 事件标志组 ──wait──▶ 各功能任务
                                              ▲
手机命令 ──▶ remote_cmd ──set bits────────────┘
                          │
                          └─ 开锁指令 ──▶ g_servo_queue ──▶ 舵机任务（串行执行）
```

- **远程命令与按键共用同一套事件标志组**，所以"手机按一下"和"手按一下"走完全相同的代码路径
- `g_mutex_function`：保证录入指纹 / 改密码 / 加卡这类需要独占人机交互的操作同一时刻只有一个
- `g_servo_queue`：所有开锁来源汇聚到一个队列，舵机动作天然串行
- MQTT 待发消息也走一个队列，由网络任务统一出队发送，避免多任务同时写 USART3 把报文交错

### 目录结构

```
CORE/          启动文件 + CMSIS 内核头
FWLIB/         ST 标准外设库（只编译用到的 8 个 .c）
FREERTOS/      FreeRTOS 内核 + heap_4 + ARM_CM4F port
HARDWARE/
  net/         ★ mqtt_packet.c  纯 MQTT 3.1.1 报文编解码（零硬件依赖，可 PC 单测）
                 bemfa_mqtt.c   巴法云接入：AT 透传 + 非阻塞联网状态机 + 心跳/重连
                 remote_cmd.c   远程命令层（WiFi 与蓝牙共用）
  as608/       AS608 指纹驱动 + 串口2 + 状态引脚
  mfrc522/     RC522 驱动 + 卡列表管理 + 刷卡解锁
  oled/        SSD1306 + 中文点阵显示
  bt24-hid/    ble.c：蓝牙透传链路（字节流 → 行）
  keyboard/ key/ servo/ beep/ LED/
SYSTEM/        sys(GPIO 位带) / delay / usart(含 USART3 接收环形缓冲) / tim
USER/          main.c（任务定义与调度）+ include.h + 中断服务 + Keil 工程
img/           OLED 锁 / 解锁图标点阵
font/          16×16 中文字库（116 字形）与索引表
mini-program/  微信小程序（控制 / 功能 / 设置 三个页面）
tests/         PC 端自检（不需要 Keil）
```

## 七、编译与烧录

1. Keil MDK5 打开 `USER/FreeRTOS.uvprojx`
2. Target 选 `STM32F407ZE`，编译器 **ARMCC (AC5)**
3. `Rebuild` → 产物在 `OBJ/demo.axf`（含 `demo.hex`）
4. J-Link / ST-Link 下载，或直接烧 `OBJ/demo.hex`

> **这个工程打开了 C99 模式**（`Options for Target → C/C++ → 勾选 C99 Mode`）。新增的网络层用了 C99 的块内声明与 `for (size_t i = ...)`，关掉会编译不过。

> **编码注意**：源码中大量中文注释为 **GB2312 编码**（Keil 默认 ANSI）。用 VS Code 等编辑器查看时请把文件编码切到 `GB2312`，否则中文会显示为乱码。修改时也请保持原编码；另外 `oled_show_string()` 依赖 GB2312 双字节切分查中文字库，**不能**把源码另存为 UTF-8。

### PC 端自检（不需要 Keil）

```bash
sh tests/run_tests.sh        # 或 Windows 下 tests\run_tests.bat
```

三步：

1. 固件 MQTT 报文层单测（`tests/test_mqtt.c`，104 项断言：定长/变长剩余长度、拆包、粘包、非法报文重同步、缓冲越界防护……）
2. 小程序 MQTT 客户端单测（`tests/test_mqtt_js.js`，会**逐字节比对**两份实现生成的 CONNECT / SUBSCRIBE / PUBLISH 报文是否完全一致）
3. 固件新增模块语法自检（`gcc -fsyntax-only` + `tests/stub/` 里的假头文件，抓拼写和原型不匹配）

> 第 3 步只是语法检查，**不能替代在 Keil 里真正编译一次**。

## 八、配置

### 8.1 巴法云 / WiFi —— `HARDWARE/net/bemfa_mqtt.h`

```c
#define WIFI_NAME       "ymq"           // ← 改成自己的 WiFi SSID（ESP8266 只支持 2.4G）
#define WIFI_PASSWORD   "88888888"      // ← 改成自己的 WiFi 密码
#define BAFA_UID        "28ecc..."      // ← 巴法云 MQTT 控制台里的「私钥」，用作 ClientID
#define BAFA_TOPIC_LOCK "lock001"       // ← 主题名，与小程序里填的保持一致
#define BAFA_HOST       "bemfa.com"
#define BAFA_PORT       9501            // MQTT 明文端口
#define MQTT_KEEPALIVE_S 60
```

### 8.2 密码 —— `HARDWARE/keyboard/keyboard.h`

```c
#define PASSWORD_MAX_LEN 6          // 密码位数
#define DEFAULT_PASSWORD "123456"   // 默认密码（可长按 KEY2 或小程序远程修改，掉电不保存）
```

### 8.3 WiFi / 蓝牙二选一 —— `USER/include.h`

```c
#define USART3_WIFI_USE 1   // 1 = USART3 接 ESP8266，跑巴法云 MQTT
#define USART3_BLE_USE  0   // 1 = USART3 接 BLE 模块，跑蓝牙透传
#define WIFI_BAUD       115200
#define BLE_BAUD        9600
```

> **硬件限制**：USART3 只有一组引脚（PB10/PB11），同一时刻只能接一个模块。所以"云端 + 蓝牙"对应**两份固件配置**，不能在同一块板子上同时用。小程序里两条通道都做了，用哪个取决于板子上插的是哪个模块。

## 九、小程序

目录 `mini-program/`，原生小程序，无任何 npm 依赖。

### 导入

1. 微信开发者工具 → 导入项目 → 目录选仓库里的 `mini-program/`
2. AppID 填你自己的（或点「测试号」）；`project.config.json` 里默认写的是 `touristappid`
3. 开发阶段勾选 **详情 → 本地设置 → 不校验合法域名、web-view、TLS 版本以及 HTTPS 证书**
   （正式发布要在小程序后台把 `wss://bemfa.com` 加进 **socket 合法域名**）

### 两种控制模式

| 模式 | 走什么 | 前提 | 适用 |
| --- | --- | --- | --- |
| 云端 MQTT | `wss://bemfa.com:9504/wss` | 设备端用 ESP8266 且已连上 WiFi | 手机在任何网络都能开锁，**真·远程** |
| 蓝牙直连 | 微信 BLE API | 设备端用 BLE 透传模块 | 手机在门锁约 10 米内，断网也能用 |

首页顶部一键切换。

### 关键配置（设置页）

| 项 | 填什么 |
| --- | --- |
| 服务器 | `wss://bemfa.com:9504/wss`（小程序只能连加密 WebSocket，巴法云对应端口 9504、路径 `/wss`） |
| 主题名 | 与固件 `BAFA_TOPIC_LOCK` 一致，例如 `lock001` |
| ClientID | **必须是设备之外的另一个 ID**，默认 `wxmp_lock01` |
| 用户名 / 密码 | 一般留空；若连接返回码是 `4/5`，去巴法云「连接密钥」页把 `appID` / `secretKey` 填到这里 |
| Service / 特征值 UUID | 留空 = 自动识别（遍历服务挑第一个可写特征值）；识别不到时先连上设备，点「列出已连接设备的全部 UUID」，点一行自动填入 |

> ⚠️ **ClientID 千万不要和设备端用同一个**。设备端用的是巴法云「私钥」，而 MQTT 规定同一 ClientID 后连的会把先连的顶下线 —— 两个都填私钥会让设备反复掉线。

### 关于 BLE 模块

微信小程序只支持 **BLE 低功耗蓝牙**，连不上 HC-05 / HC-06 这类**经典蓝牙 SPP** 模块。请用 BLE 串口透传模块（JDY-24M 之类，常见服务 `0000FFE0`、特征值 `0000FFE1`）。

## 十、已知取舍

- **密码与卡号不持久化**：改过的密码、登记的卡都存在 RAM，掉电复位后回到默认值（需加 24C02/Flash 才能掉电保存）
- **设备到云端是明文 MQTT**：STM32 侧走 `bemfa.com:9501` 明文端口（小程序到云端那段是加密的 wss）。上公网请改用 9503 + TLS，本工程未实现
- **远程开锁只靠主题隔离**：知道私钥和主题名的人都能发 `unlock`，没有额外口令校验，适合个人 / 内网使用
- **MQTT 断线判定靠静默超时**：正常在线每 30 s 一次心跳应答，连续 90 s 收不到任何字节才判掉线重连，所以从掉线到重连最多约 1.5 分钟
- **刷卡轮询为 5 秒节流**：`check_card_unlock()` 命中后 5 秒内不再重复触发，防止一张卡连开多次
- **指纹库容量按 100 枚**：`AS608Para.PS_max = 100`，指纹 ID 直接取当前有效模板数，"删除"是清空整库而非按 ID 删
- **`fp_add` / `card_add` 只是"进入流程"**：远程只能让设备开始等待，按手指、刷卡这两步必须有人在设备旁边完成

## 十一、开源协议

[MIT](LICENSE) © ymq
