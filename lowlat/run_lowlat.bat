@echo off
rem ---------------------------------------------------------------
rem  Aviateur low-latency receiver launcher
rem  Usage: run_lowlat.bat [RX_URBS]      (default 8 = driver default)
rem  Writes console output (driver + WFB-ng messages) to
rem  logs\console_<time>.txt. The app itself also writes
rem  logs\aviateur_<time>.log and logs\link_<time>.csv.
rem ---------------------------------------------------------------
setlocal
cd /d "%~dp0"
if not exist "logs" mkdir "logs"

set URBS=%1
if "%URBS%"=="" set URBS=8
set DEVOURER_RX_URBS=%URBS%

rem USB RX ring telemetry once per second (goes to the console log).
set DEVOURER_RX_RING_MS=1000

for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set TS=%%i
echo [%TS%] DEVOURER_RX_URBS=%DEVOURER_RX_URBS% DEVOURER_RX_RING_MS=%DEVOURER_RX_RING_MS% > "logs\console_%TS%_urbs%URBS%.txt"
aviateur.exe >> "logs\console_%TS%_urbs%URBS%.txt" 2>&1
endlocal
