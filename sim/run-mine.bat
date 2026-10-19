@echo off
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>nul
if not errorlevel 1 goto :py_ok
where py >nul 2>nul
if errorlevel 1 (
  echo Python 3 not found. Install Python 3.8+ and add it to PATH, then run again.
  pause
  exit /b 1
)
set "PY=py -3"
:py_ok

echo ==========================================================
echo   Virtual door lock - bind to YOUR bemfa topic
echo ==========================================================
echo   Find the private key in bemfa console:
echo     open the topic card  ->  "more settings"  ->  key
echo   Topic name and key must belong to the SAME topic,
echo   otherwise the console shows "subscriber: offline".
echo ==========================================================
echo.

set "TP=lock000"
set /p "TP=Topic name [lock000]: "
if "%TP%"=="" set "TP=lock000"

set "UID="
set /p "UID=Private key (leave empty = built-in demo key): "
echo.

if "%UID%"=="" goto USE_DEMO
echo [i] topic=%TP%  key=%UID%
%PY% -m mocklock.app --topic %TP% --uid %UID%
goto DONE

:USE_DEMO
echo [!] using built-in demo key, it only works with topic lock000
%PY% -m mocklock.app --topic %TP%

:DONE
echo.
echo Process exited.
pause
