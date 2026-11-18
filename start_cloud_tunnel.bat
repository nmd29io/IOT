@echo off
title Flood Monitor - Cloud du phong (Cloudflare Tunnel)
powershell -ExecutionPolicy Bypass -File "%~dp0start_cloud_tunnel.ps1" %*
pause
