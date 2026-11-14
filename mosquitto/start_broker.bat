@echo off
:: start_broker.bat — Start Mosquitto MQTT Broker on Windows
:: Place this file in the mosquitto\ folder next to mosquitto.conf
:: Requires Mosquitto to be installed: https://mosquitto.org/download/

setlocal

:: ─── Adjust this path if Mosquitto is installed elsewhere ────────────────────
set MOSQUITTO_EXE=C:\Program Files\Mosquitto\mosquitto.exe

:: ─── Config file (relative to this batch file's directory) ──────────────────
set CONF_FILE=%~dp0mosquitto.conf

:: ─── Create data dir if missing ──────────────────────────────────────────────
if not exist "%~dp0data" mkdir "%~dp0data"

echo.
echo  ╔══════════════════════════════════════════════╗
echo  ║   Urban Flood Monitor — MQTT Broker           ║
echo  ║   Mosquitto listening on port 1883            ║
echo  ╚══════════════════════════════════════════════╝
echo.

:: Check if executable exists
if not exist "%MOSQUITTO_EXE%" (
    echo [ERROR] Mosquitto not found at: %MOSQUITTO_EXE%
    echo         Download from https://mosquitto.org/download/
    echo         Then update MOSQUITTO_EXE in this script.
    pause
    exit /b 1
)

echo [INFO] Starting broker with config: %CONF_FILE%
echo [INFO] Press Ctrl+C to stop.
echo.

"%MOSQUITTO_EXE%" -v -c "%CONF_FILE%"

echo.
echo [INFO] Broker stopped.
pause
