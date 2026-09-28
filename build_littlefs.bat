@echo off
set DATA_DIR=%~dp0ESP32-S3-LittleFS-WebServer\data
set OUTPUT=%~dp0flasher\firmware\ffat.bin
set SIZE=0xBE0000

for /f "delims=" %%i in ('dir /b /s "C:\Users\user\AppData\Local\Arduino15\packages\esp32\tools\mklittlefs\4.0.2-db0513a" 2^>nul') do set MK=%%i

if not defined MK (
  echo [ERROR] mkfatfs.exe not found.
  pause
  exit /b 1
)

echo Using: %MK%
echo Packing: %DATA_DIR%
echo Output:  %OUTPUT%
echo Size:    %SIZE%
echo.

"%MK%" -c "%DATA_DIR%" -s %SIZE% "%OUTPUT%"

if %ERRORLEVEL%==0 (
  echo [OK] ffat.bin created successfully!
) else (
  echo [ERROR] Build failed.
)
pause