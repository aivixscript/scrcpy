@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "PATH=D:\msys64\mingw64\bin;D:\Android\Sdk\platform-tools;%PATH%"
set "SCRCPY_SERVER_PATH=%cd%\x\server\scrcpy-server"
set "SCRCPY_ICON_DIR=%cd%\app\data"

echo Starting scrcpy for every connected device...
echo Press SYNC in each window to mirror input.
echo.

for /f "tokens=1,2" %%A in ('adb devices') do (
  if /I "%%B"=="device" (
    echo Device: %%A
    start "scrcpy-%%A" "%~dp0x\app\scrcpy.exe" --serial=%%A --no-window-aspect-ratio-lock --render-fit=stretched --keyboard=uhid %*
    timeout /t 1 /nobreak >nul
  )
)

echo Done.
