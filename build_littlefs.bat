@echo off
cd /d "%~dp0"
python tools\build_firmware.py
if errorlevel 1 (
  echo [ERROR] Firmware build failed.
  pause
  exit /b 1
)
echo [OK] Board firmware, PS4 firmware variants, and manifests were built.
pause
