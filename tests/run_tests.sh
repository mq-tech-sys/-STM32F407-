#!/bin/sh
# =============================================================================
#  smart-lock 网络层自检（PC 端，不需要 Keil / ARM 工具链）
#
#   1) MQTT 报文编解码单元测试  —— 真的执行，104 项断言
#   2) 新增固件模块语法自检     —— -fsyntax-only，抓拼写/原型不匹配
#
#  用法： sh tests/run_tests.sh
#         （Windows 上也可以直接跑 tests/run_tests.bat）
# =============================================================================
set -e
cd "$(dirname "$0")/.."

CC="${CC:-gcc}"
CFLAGS="-std=c99 -Wall -Wextra"

echo "=============================================="
echo " 1/6  固件 MQTT 报文层单元测试"
echo "=============================================="
$CC $CFLAGS -I HARDWARE/net \
    tests/test_mqtt.c HARDWARE/net/mqtt_packet.c \
    -o tests/test_mqtt.exe
./tests/test_mqtt.exe

echo
echo "=============================================="
echo " 2/6  小程序单元测试（MQTT / 蓝牙 / 功能页）"
echo "=============================================="
NODE="${NODE:-node}"
$NODE tests/test_mqtt_js.js
$NODE tests/test_ble.js
$NODE tests/test_features.js

echo
echo "=============================================="
echo " 3/6  小程序界面静态检查（wxml / 图标 / 主题变量）"
echo "=============================================="
$NODE tests/test_ui.js

echo
echo "=============================================="
echo " 4/6  敏感信息扫描（密钥 / AppID 不能进仓库）"
echo "=============================================="
$NODE tests/check_secrets.js

echo
echo "=============================================="
echo " 5/6  固件新增模块语法自检（stub 头文件）"
echo "=============================================="
$CC -fsyntax-only $CFLAGS \
    -I tests/stub -I HARDWARE/net -I HARDWARE/bt24-hid -I HARDWARE/param \
    HARDWARE/net/bemfa_mqtt.c \
    HARDWARE/net/remote_cmd.c \
    HARDWARE/bt24-hid/ble.c \
    HARDWARE/param/param.c
echo "  bemfa_mqtt.c / remote_cmd.c / ble.c / param.c  语法 ok"

echo
echo "=============================================="
echo " 6/6  掉电保存单元测试（密码 / 卡片）"
echo "=============================================="
$CC -std=c99 -Wall -Wextra -DPARAM_HOST_TEST -I tests/stub -I HARDWARE/param \
    HARDWARE/param/param.c tests/test_param.c -o tests/test_param.exe
tests/test_param.exe || exit 1

echo
echo "全部通过。"
echo "注意：第 5 步只做语法检查，正式编译请在 Keil MDK 里 Rebuild 一次。"
