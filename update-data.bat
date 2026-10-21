@echo off
chcp 65001 >nul
title Update data folder from GitHub
echo.
echo  ==========================================
echo   Update data/ from ps4freeforall archive
echo  ==========================================
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-data.ps1"
echo.
pause