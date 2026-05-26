@echo off
chcp 65001 >nul 2>&1
title NATMesh Windows Build

echo ============================================
echo    NATMesh -- Windows One-Click Build
echo ============================================
echo.

where pwsh.exe >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    pwsh.exe -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1"
) else (
    powershell.exe -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1"
)

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed -- see log above.
    pause
    exit /b 1
)

echo.
echo ============================================
echo    Build complete! Press any key to exit.
echo ============================================
pause >nul
