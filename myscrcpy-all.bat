@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "PATH=D:\msys64\mingw64\bin;D:\Android\Sdk\platform-tools;%PATH%"
set "SCRCPY_SERVER_PATH=%cd%\x\server\scrcpy-server"
set "SCRCPY_ICON_DIR=%cd%\app\data"

echo Cleaning leftover ADB tunnels...
adb reverse --remove-all >nul 2>&1
adb forward --remove-all >nul 2>&1

echo Starting scrcpy for every connected device...
echo Press SYNC and pick the other window.
echo.

for /f "tokens=1,2" %%A in ('adb devices') do (
  if /I "%%B"=="device" (
    echo Device: %%A
    echo %%A | findstr ":" >nul
    if errorlevel 1 (
      start "scrcpy-%%A" "%~dp0x\app\scrcpy.exe" --serial=%%A --no-window-aspect-ratio-lock --render-fit=stretched --keyboard=uhid --no-audio %*
    ) else (
      start "scrcpy-%%A" "%~dp0x\app\scrcpy.exe" --serial=%%A --no-window-aspect-ratio-lock --render-fit=stretched --keyboard=uhid --no-audio --force-adb-forward %*
    )
    timeout /t 3 /nobreak >nul
  )
)

echo Done.
