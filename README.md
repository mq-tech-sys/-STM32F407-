# 基于 STM32F407 的智能指纹门锁

> 一块 F407 + 一颗 AS608 指纹头 + 一张 RC522 读卡器，做出**指纹 / 密码 / 刷卡 / WiFi / 蓝牙**五种开锁方式的成品级门锁固件。
> 基于 **FreeRTOS** 多任务架构，标准库（SPL）开发，Keil MDK5 一键编译烧录。

---

## 一、功能一览

| 开锁方式 | 说明 |
| --- | --- |
| 指纹 | AS608 指纹头，UART 通信；支持录入 / 匹配 / 清空指纹库，匹配带得分显示 |
| 密码 | 4×4 矩阵键盘输入 6 位数字密码，OLED 上以 `*` 掩码回显，支持修改密码 |
| RFID 刷卡 | MFRC522 读头，最多登记 10 张卡；**自动轮询刷卡**，未授权卡蜂鸣告警 |
| WiFi 远程 | ESP8266 + 巴法云（TCP 直连），手机端下发 `on`/`off` 开锁，锁状态实时上报 |
| 蓝牙远程 | JDY-24 类串口透传模块，收 `on`/`off` 开锁 |

- 0.96" OLED 实时显示锁 / 解锁图标与全部中文提示（"请按指纹!"、"未授权卡"、"卡添加成功!" 等 116 字形）
- 舵机开锁后 **2 秒自动回锁**，锁状态同步上报到手机端
- 蜂鸣器操作提示音；串口 115200 输出完整调试日志

## 二、技术栈

| 项目 | 说明 |
| --- | --- |
| MCU | STM32F407ZET6（168 MHz，512K Flash / 192K RAM，LQFP144） |
| 库 | ST 标准外设库 SPL v1.4.0（**非** HAL/LL），`USE_STDPERIPH_DRIVER` |
| RTOS | FreeRTOS 10.x，抢占式调度，`configTICK_RATE_HZ = 1000`，heap_4，堆 75 KB |
| 工具链 | Keil MDK 5 + ARMCC（AC5），工程文件 `USER/FreeRTOS.uvprojx` |
| 语言 | C（gnu99），自研代码约 2500 行（不含 SPL / FreeRTOS） |

## 三、硬件接线

| 模块 | 接口 | 引脚 | 备注 |
| --- | --- | --- | --- |
| AS608 指纹头 | USART2 | PA2 / PA3 | 57600 bps |
| ESP8266 / 蓝牙模块 | USART3 | PB10 / PB11 | WiFi 115200 / 蓝牙 9600，**二选一** |
| MFRC522 读卡器 | 软件 SPI | SCK `PD7`、MOSI `PC6`、MISO `PC8`、CS `PD6`、RST `PC11` | 非硬件 SPI |
| OLED 0.96" (SSD1306) | 软件 I2C | SCL `PB15`、SDA `PD10` | 128×64 |
| SG90 舵机 | TIM4_CH3 PWM | `PD14` | 50 Hz，0.5 ms=关锁 / 1.5 ms=开锁 |
| 蜂鸣器 | GPIO | `PF8` | 高电平响 |
| 独立按键 ×3 | GPIO | KEY1 `PA0`、KEY2 `PE2`、KEY3 `PE3` | 低电平有效 |
| 4×4 矩阵键盘 | GPIO 扫描 | 行：`PB7` `PA4` `PG15` `PC7`<br>列：`PC9` `PB6` `PE6` `PA8` | 行列扫描 + 20 ms 消抖 |
| 调试串口 | USART1 | PA9 / PA10 | 115200 bps，`printf` 输出 |

> 接线定义集中在各模块头文件里（`oled.h`、`beep.h`、`key.h`、`MFRC522.h` 等），换板子只改这几个宏。

## 四、操作说明

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

## 五、软件架构

FreeRTOS 多任务，**一个硬件模块一个任务**，任务间用事件标志组 / 消息队列 / 互斥量解耦：

| 任务 | 栈 | 职责 |
| --- | --- | --- |
| `task_init` | 512 | 建队列/锁/事件组，初始化全部外设，拉起其余任务后自删 |
| `task_oled` | 512 | 检测 `g_lock_state` 变化，刷新锁 / 解锁图标 |
| `task_servo` | 512 | 从 `g_servo_queue` 取指令，驱动舵机开/关锁，2 秒自动回锁并上报状态 |
| `task_as608` | 512 | 短按事件 → 录入 / 匹配 / 清空指纹 |
| `task_keyboard` | 512 | 长按事件 → 密码开锁 / 修改密码 |
| `task_rfid` | 512 | 双击事件 → 添加卡 / 清空卡列表 |
| `task_rfid_unlock` | 512 | 100 ms 轮询刷卡，命中已登记卡即开锁 |
| `task_key` | 512 | 10 ms 扫描 3 个独立按键，产生短按/长按/双击事件 |
| `task_esp8266` / `task_ble24` | 512 | 10 ms 处理巴法云 / 蓝牙报文，60 s 心跳 |

**并发模型**

```
按键任务 ──set bits──▶ 事件标志组 ──wait──▶ 各功能任务
                                              │
                              g_mutex_function（功能互斥锁）
                                              │
开锁来源（指纹/密码/刷卡/WiFi/蓝牙）──▶ g_servo_queue ──▶ 舵机任务（串行执行）
```

- `g_mutex_function`：保证"录入指纹 / 改密码 / 加卡"这类需要独占人机交互的操作同一时刻只有一个
- `g_servo_queue`：所有开锁来源汇聚到一个队列，舵机动作天然串行，不会互相打断
- `g_mutex_printf`：串口调试输出加锁，避免多任务穿插乱码

### 目录结构

```
CORE/       启动文件 + CMSIS 内核头
FWLIB/      ST 标准外设库（只编译用到的 8 个 .c）
FREERTOS/   FreeRTOS 内核 + heap_4 + ARM_CM4F port
HARDWARE/   ★ 各硬件驱动：as608 / mfrc522 / oled / esp8266 / bt24-hid / keyboard / key / servo / beep / LED
SYSTEM/     sys(GPIO 位带) / delay / usart / tim
USER/       main.c（任务定义与调度）+ include.h + 中断服务 + Keil 工程
img/        OLED 锁 / 解锁图标（48×48、32×32 点阵数组）
font/       16×16 中文字库（116 字形）与索引表
```

## 六、编译与烧录

1. Keil MDK5 打开 `USER/FreeRTOS.uvprojx`
2. Target 选 `STM32F407ZE`，编译器 **ARMCC (AC5)**
3. `Rebuild` → 产物在 `OBJ/demo.axf`（含 `demo.hex`）
4. J-Link / ST-Link 下载，或直接烧 `OBJ/demo.hex`

> **注意**：源码中大量中文注释为 **GB2312 编码**（Keil 默认 ANSI）。用 VS Code 等编辑器查看时请把文件编码切到 `GB2312`，否则中文会显示为乱码。修改中文注释时也请保持原编码，直接另存为 UTF-8 会导致乱码。

## 七、首次使用需要改的配置

`HARDWARE/esp8266/esp8266.h`：

```c
#define WIFI_NAME     "ymq"        // ← 改成自己的 WiFi SSID
#define WIFI_PASSWORD "88888888"   // ← 改成自己的 WiFi 密码
#define BAFA_UID      "28eccdb351e4bf84ad1390c143846844"  // ← 巴法云私钥
#define BAFA_TOPIC_LOCK "lock001"  // ← 巴法云主题名
```

`HARDWARE/keyboard/keyboard.h`：

```c
#define PASSWORD_MAX_LEN 6          // 密码位数
#define DEFAULT_PASSWORD "123456"   // 默认密码（可长按 KEY2 修改，掉电不保存）
```

WiFi / 蓝牙二选一在 `USER/include.h`：

```c
#define USART3_WIFI_USE 1   // 用 ESP8266 WiFi
#define USART3_BLE_USE  0   // 用蓝牙模块
```

## 八、已知取舍

- **密码与卡号不持久化**：改过的密码、登记的卡都存在 RAM，掉电复位后回到默认值（需加 24C02/Flash 才能掉电保存）
- **刷卡轮询为 5 秒节流**：`check_card_unlock()` 命中后 5 秒内不再重复触发，防止一张卡连开多次
- **指纹库容量按 100 枚**：`AS608Para.PS_max = 100`，指纹 ID 直接取当前有效模板数，删除是"清空整库"而非按 ID 删
- **中文显示走 GB2312 字库查表**：`oled_show_string()` 按 2 字节汉字切分索引，因此不支持 UTF-8 源文件中的中文
- **无线开锁无鉴权**：巴法云 / 蓝牙收到 `on` 即开锁，仅适合演示与内网环境，上公网需自行加白名单或校验

## 九、开源协议

[MIT](LICENSE) © 叶铭强 (YmqMQ)
