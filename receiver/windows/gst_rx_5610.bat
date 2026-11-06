@echo off
title GStreamer RX 5610 (direct LAN)
cd /d "C:\Program Files\gstreamer\1.0\msvc_x86_64\bin"
gst-launch-1.0 -v udpsrc port=5610 buffer-size=2097152 caps="application/x-rtp,media=video,clock-rate=90000,encoding-name=H264,payload=96" ! rtph264depay ! h264parse ! d3d11h264dec compliance=flexible ! d3d11videosink sync=false
pause
