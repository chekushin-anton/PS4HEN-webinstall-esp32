@echo off
setlocal enabledelayedexpansion

REM ==== НАСТРОЙКИ ====
REM Впишите IP ESP32 (Serial Monitor покажет адрес)
set ESP_IP=192.168.4.1

REM Папка, содержимое которой заливаем
set DATA_DIR=%~dp0ESP32-S3-LittleFS-WebServer\data

REM ==== ПРОВЕРКИ ====
if not exist "%DATA_DIR%" (
  echo [ERROR] Data folder not found: %DATA_DIR%
  pause
  exit /b 1
)

where curl >nul 2>nul
if errorlevel 1 (
  echo [ERROR] curl not found. Install: https://curl.se/windows/
  pause
  exit /b 1
)

echo.
echo === Uploading %DATA_DIR% to http://%ESP_IP%/ ===
echo.

set COUNT=0
set ERRORS=0

cd /d "%DATA_DIR%"
for /r %%F in (*) do (
  set "FULL=%%F"
  set "REL=!FULL:%DATA_DIR%\=!"
  set "REL=!REL:\=/!"
  set /a COUNT+=1
  echo [!COUNT!] !REL!
  curl -s -S -X POST -F "file=@%%F" "http://%ESP_IP%/upload?path=!REL!"
  if errorlevel 1 (
    set /a ERRORS+=1
    echo    ^^^^ ERROR on !REL!
  )
)

echo.
echo === Uploaded %COUNT% files, errors: %ERRORS% ===
echo.
echo Verify: open http://%ESP_IP%/api/list in browser
pause