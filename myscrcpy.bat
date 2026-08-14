@echo off
setlocal
cd /d "%~dp0"

set "PATH=D:\msys64\mingw64\bin;D:\Android\Sdk\platform-tools;%PATH%"
set "SCRCPY_SERVER_PATH=%cd%\x\server\scrcpy-server"
set "SCRCPY_ICON_DIR=%cd%\app\data"

rem Fixed launch options for this project
rem Overlay buttons: SYNC / BACK / HOME (no hotkeys needed for sync)
"%~dp0x\app\scrcpy.exe" --no-window-aspect-ratio-lock --render-fit=stretched %*
