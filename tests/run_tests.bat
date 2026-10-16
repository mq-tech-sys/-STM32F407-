@echo off
rem ============================================================================
rem  smart-lock 网络层自检（PC 端，不需要 Keil / ARM 工具链）
rem
rem   1) MQTT 报文编解码单元测试  -- 真的执行，104 项断言
rem   2) 新增固件模块语法自检     -- -fsyntax-only，抓拼写/原型不匹配
rem
rem  用法： tests\run_tests.bat
rem ============================================================================
setlocal
cd /d "%~dp0.."

where gcc >nul 2>nul
if errorlevel 1 (
    echo [错误] 找不到 gcc。请把 MinGW / TDM-GCC 的 bin 目录加进 PATH，
    echo        或用： set CC=D:\mingw64\bin\gcc.exe 之后重跑。
    exit /b 1
)

echo ==============================================
echo  1/3  固件 MQTT 报文层单元测试
echo ==============================================
gcc -std=c99 -Wall -Wextra -I HARDWARE\net ^
    tests\test_mqtt.c HARDWARE\net\mqtt_packet.c ^
    -o tests\test_mqtt.exe
if errorlevel 1 exit /b 1
tests\test_mqtt.exe
if errorlevel 1 exit /b 1

echo.
echo ==============================================
echo  2/3  小程序 MQTT 客户端单元测试
echo ==============================================
node tests\test_mqtt_js.js
if errorlevel 1 exit /b 1

echo.
echo ==============================================
echo  3/3  固件新增模块语法自检（stub 头文件）
echo ==============================================
gcc -fsyntax-only -std=c99 -Wall -Wextra ^
    -I tests\stub -I HARDWARE\net -I HARDWARE\bt24-hid ^
    HARDWARE\net\bemfa_mqtt.c ^
    HARDWARE\net\remote_cmd.c ^
    HARDWARE\bt24-hid\ble.c
if errorlevel 1 exit /b 1
echo   bemfa_mqtt.c / remote_cmd.c / ble.c  语法 ok

echo.
echo 全部通过。
echo 注意：第 3 步只做语法检查，正式编译请在 Keil MDK 里 Rebuild 一次。
endlocal
