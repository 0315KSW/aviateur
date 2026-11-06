@echo off
chcp 65001 >nul
setlocal
set AGX=192.168.0.68
set /p AGXUSER=AGX login ID: 

echo [1/4] Closing Aviateur (port 5600)...
taskkill /im aviateur.exe /f >NUL 2>&1

echo [2/4] Starting GStreamer receiver window...
start "GStreamer RX 5600" "%~dp0gst_rx_5600.bat"

echo [3/4] Copying logger to AGX...
scp "%~dp0wfb_link_log.py" %AGXUSER%@%AGX%:wfb_link_log.py
if errorlevel 1 (echo Copy failed. Check ID/password/network. & pause & exit /b 1)

echo [4/4] Running logger on AGX. Press Ctrl+C here to stop.
ssh -t %AGXUSER%@%AGX% "sudo pkill -f [g]st-launch-1.0; python3 ~/wfb_link_log.py"

echo Downloading CSV logs from AGX to this folder...
scp "%AGXUSER%@%AGX%:wfb_logs/*.csv" "%~dp0"
echo Done. CSV files are in: %~dp0
pause
