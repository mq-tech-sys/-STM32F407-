# STM32F407 Smart Fingerprint Door Lock

> An STM32F407 + AS608 fingerprint sensor + MFRC522 RFID reader door lock firmware with **five unlock methods**: fingerprint, keypad password, RFID card, WiFi and Bluetooth.
> Built on **FreeRTOS** with the ST Standard Peripheral Library (SPL), compiles and flashes from Keil MDK5.

[中文说明 (Chinese)](README.md)

---

## Features

| Method | Details |
| --- | --- |
| Fingerprint | AS608 over UART; enroll / match / clear the template library, match score shown on screen |
| Password | 6-digit password on a 4×4 matrix keypad, masked with `*` on the OLED, changeable at runtime |
| RFID card | MFRC522 reader, up to 10 registered cards, **auto-polling**; unknown cards trigger a buzzer alarm |
| WiFi | ESP8266 + Bemfa cloud (raw TCP); send `on` / `off` from the phone, lock state is reported back |
| Bluetooth | Serial-transparent BLE module on the same UART; accepts `on` / `off` |

- 0.96" OLED showing lock / unlock icons and Chinese status messages
- Servo auto-relocks **2 seconds** after unlocking, and the state is pushed to the phone
- Buzzer feedback plus a full 115200 bps debug log over USART1

## Tech stack

| Item | Detail |
| --- | --- |
| MCU | STM32F407ZET6 @168 MHz, 512K Flash / 192K RAM |
| Library | ST Standard Peripheral Library v1.4.0 (**not** HAL/LL) |
| RTOS | FreeRTOS 10.x, preemptive, 1 kHz tick, heap_4, 75 KB heap |
| Toolchain | Keil MDK 5 + ARMCC (AC5), project `USER/FreeRTOS.uvprojx` |

## Wiring

| Module | Bus | Pins |
| --- | --- | --- |
| AS608 fingerprint | USART2 (57600) | PA2 / PA3 |
| ESP8266 or BLE module | USART3 | PB10 / PB11 |
| MFRC522 | bit-banged SPI | SCK `PD7`, MOSI `PC6`, MISO `PC8`, CS `PD6`, RST `PC11` |
| OLED SSD1306 | bit-banged I2C | SCL `PB15`, SDA `PD10` |
| SG90 servo | TIM4_CH3 PWM 50 Hz | `PD14` |
| Buzzer | GPIO | `PF8` |
| Push buttons ×3 | GPIO | KEY1 `PA0`, KEY2 `PE2`, KEY3 `PE3` |
| 4×4 keypad | GPIO scan | rows `PB7` `PA4` `PG15` `PC7`, cols `PC9` `PB6` `PE6` `PA8` |
| Debug UART | USART1 (115200) | PA9 / PA10 |

## Controls

| Button | Short press | Double click | Long press |
| --- | --- | --- | --- |
| KEY1 | Enroll fingerprint | Add RFID card | Password unlock |
| KEY2 | Verify fingerprint | Erase all cards | Change password |
| KEY3 | Erase all fingerprints | — | — |

Keypad: `0`–`9` input, `*` backspace, `#` submit.

## Architecture

One FreeRTOS task per hardware module, decoupled by event groups, queues and mutexes.

```
key task ──set bits──▶ event group ──wait──▶ feature tasks
                                                │
                              g_mutex_function (exclusive human interaction)
                                                │
any unlock source ────────────────────▶ g_servo_queue ──▶ servo task (serialized)
```

Every unlock source funnels into a single queue, so the servo is driven strictly one command at a time; `g_mutex_function` guarantees only one enrolment / password / card operation occupies the UI at any moment.

```
CORE/       startup file + CMSIS
FWLIB/      ST SPL (only the 8 drivers actually used are compiled)
FREERTOS/   kernel + heap_4 + ARM_CM4F port
HARDWARE/   as608, mfrc522, oled, esp8266, bt24-hid, keyboard, key, servo, beep, LED
SYSTEM/     sys (bit-band GPIO), delay, usart, tim
USER/       main.c, include.h, interrupts, Keil project
img/        OLED lock/unlock bitmaps
font/       16×16 Chinese font table (116 glyphs) + index
```

## Build

1. Open `USER/FreeRTOS.uvprojx` in Keil MDK5
2. Select target `STM32F407ZE`, compiler **ARMCC (AC5)**
3. `Rebuild` → output in `OBJ/demo.axf` / `OBJ/demo.hex`
4. Flash with J-Link / ST-Link

> **Encoding note:** most Chinese comments in the sources are **GB2312** (Keil's default ANSI). Set your editor to `GB2312` when viewing, and keep that encoding when editing — saving as UTF-8 will corrupt the comments. `oled_show_string()` also relies on 2-byte GB2312 characters for its font lookup.

## Configuration

Before first use, edit `HARDWARE/esp8266/esp8266.h` (WiFi SSID / password / Bemfa UID / topic), `HARDWARE/keyboard/keyboard.h` (default password) and pick WiFi or Bluetooth in `USER/include.h` via `USART3_WIFI_USE` / `USART3_BLE_USE`.

## Known limitations

- Passwords and registered cards live in RAM only — they reset on power loss (add an EEPROM/Flash to persist)
- Card polling is throttled to one unlock per 5 seconds
- Fingerprint library is capped at 100 templates and "delete" clears the whole library rather than a single ID
- Remote unlock has no authentication beyond the cloud topic; suitable for demos and LAN use only

## License

[MIT](LICENSE) © 叶铭强 (YmqMQ)
