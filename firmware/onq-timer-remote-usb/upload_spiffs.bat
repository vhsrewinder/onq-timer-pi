@echo off
echo =======================================
echo    ESP32 SPIFFS Upload (Config-based)
echo =======================================

set SCRIPT_DIR=%~dp0
set CONFIG_FILE=%SCRIPT_DIR%esp32_spiffs_config.json

echo Checking for configuration file...
if not exist "%CONFIG_FILE%" (
    echo ERROR: Configuration file not found: %CONFIG_FILE%
    echo Please create esp32_spiffs_config.json in your project directory
    echo.
    pause
    exit /b 1
)

echo Configuration file found: %CONFIG_FILE%
echo.

echo Reading configuration and uploading SPIFFS...
python "%SCRIPT_DIR%esp32_spiffs_upload.py" --config "%CONFIG_FILE%"

if %ERRORLEVEL% equ 0 (
    echo.
    echo =======================================
    echo     SPIFFS Upload Successful!
    echo =======================================
) else (
    echo.
    echo =======================================
    echo       SPIFFS Upload Failed!
    echo =======================================
)

echo.
pause