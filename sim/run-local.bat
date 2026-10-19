@echo off
setlocal
cd /d "%~dp0"
chcp 65001 >nul

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

echo Starting virtual door lock (LOCAL only, no bemfa) ...
echo.
%PY% -m mocklock.app --mode local %*

echo.
echo Process exited.
pause
