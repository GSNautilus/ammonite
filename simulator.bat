@echo off
rem Start the Ammonite panel simulator. Double-click to (re)start:
rem   1. closes any simulator already running (it holds the DLL open)
rem   2. rebuilds sim\synthcore.dll from core\
rem   3. launches the simulator
rem Python: set AMMONITE_PYTHON to a python.exe with numpy, pygame,
rem sounddevice and ziglang; otherwise "python" from PATH is used.
setlocal
if defined AMMONITE_PYTHON (set PY=%AMMONITE_PYTHON%) else (set PY=python)
set SIM=%~dp0sim

powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -like '*panel_sim*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; Write-Host ('closed running simulator (pid ' + $_.ProcessId + ')') }"

echo Building synthcore.dll ... (the very first build on a fresh zig cache
echo takes ~2 min and floods the console with libc++ warnings - harmless)
powershell -NoProfile -ExecutionPolicy Bypass -File "%SIM%\build_dll.ps1"
if errorlevel 1 (
    echo.
    echo DLL build failed, not starting the simulator.
    pause
    exit /b 1
)

"%PY%" "%SIM%\panel_sim.py"
