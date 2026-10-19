@echo off
chcp 936 >nul
title smart-lock 网络层自检
setlocal
rem ============================================================================
rem  smart-lock 网络层自检（PC 端，不需要 Keil / ARM 工具链）
rem
rem  用法： 双击本文件，或执行  tests\run_tests.bat
rem         脚本/CI 里不想停住时加参数：  tests\run_tests.bat /nopause
rem ============================================================================

cd /d "%~dp0.."
echo 项目目录: %CD%
echo.

rem ---------- 定位 gcc ----------
set "CC="
where gcc >nul 2>nul
if not errorlevel 1 set "CC=gcc"
if not defined CC if exist "D:/mingw64/bin/gcc.exe" set "CC=D:/mingw64/bin/gcc.exe"
if not defined CC if exist "C:/mingw64/bin/gcc.exe" set "CC=C:/mingw64/bin/gcc.exe"
if not defined CC if exist "C:/TDM-GCC-64/bin/gcc.exe" set "CC=C:/TDM-GCC-64/bin/gcc.exe"
if not defined CC if exist "C:/msys64/mingw64/bin/gcc.exe" set "CC=C:/msys64/mingw64/bin/gcc.exe"
if not defined CC (
    echo [错误] 找不到 gcc 编译器。
    echo        请安装 MinGW-w64，或把 gcc.exe 所在目录加入 PATH 后重试。
    goto :fail
)

rem ---------- 定位 node ----------
set "NODE="
where node >nul 2>nul
if not errorlevel 1 set "NODE=node"
if not defined NODE if exist "C:/Program Files/nodejs/node.exe" set "NODE=C:/Program Files\nodejs\node.exe"
if not defined NODE (
    echo [错误] 找不到 node，第 2 步小程序测试需要它。
    echo        请安装 Node.js 后重试，或只用:  run_tests.bat 1
    goto :fail
)

echo 编译器: %CC%
echo Node  : %NODE%
echo.

echo ==============================================
echo  1/5  固件 MQTT 报文层单元测试
echo ==============================================
"%CC%" -std=c99 -Wall -Wextra -I HARDWARE\net tests\test_mqtt.c HARDWARE\net\mqtt_packet.c -o tests\test_mqtt.exe
if errorlevel 1 goto :fail
tests\test_mqtt.exe
if errorlevel 1 goto :fail

echo.
echo ==============================================
echo  2/5  小程序单元测试（MQTT / 蓝牙 / 功能页）
echo ==============================================
"%NODE%" tests\test_mqtt_js.js
if errorlevel 1 goto :fail
"%NODE%" tests\test_ble.js
if errorlevel 1 goto :fail
"%NODE%" tests\test_features.js
if errorlevel 1 goto :fail

echo.
echo ==============================================
echo  3/5  小程序界面静态检查（wxml / 图标 / 主题变量）
echo ==============================================
"%NODE%" tests\test_ui.js
if errorlevel 1 goto :fail

echo.
echo ==============================================
echo  4/5  敏感信息扫描（密钥 / AppID 不能进仓库）
echo ==============================================
"%NODE%" tests\check_secrets.js
if errorlevel 1 goto :fail

echo.
echo ==============================================
echo  5/5  固件新增模块语法自检（stub 头文件）
echo ==============================================
"%CC%" -fsyntax-only -std=c99 -Wall -Wextra -I tests\stub -I HARDWARE\net -I HARDWARE\bt24-hid HARDWARE\net\bemfa_mqtt.c HARDWARE\net\remote_cmd.c HARDWARE\bt24-hid\ble.c
if errorlevel 1 goto :fail
echo   bemfa_mqtt.c / remote_cmd.c / ble.c  语法 ok

echo.
echo ----------------------------------------------
echo  全部通过（5/5）。
echo  注意：第 5 步只做语法检查，正式固件请到 Keil MDK 里 Rebuild 一次。
echo ----------------------------------------------
set "RC=0"
goto :finish

:fail
echo.
echo ----------------------------------------------
echo  自检失败，见上方报错。
echo ----------------------------------------------
set "RC=1"

:finish
echo.
rem 双击运行时 cmd 会在脚本结束的瞬间关窗口，这里停住，否则只能看到一闪
if /i "%~1"=="/nopause" goto :done
echo 按任意键关闭窗口 ...
pause >nul

:done
endlocal & exit /b %RC%
