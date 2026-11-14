# simulate_nodes.ps1 - Chay bo mo phong 2 node (chu ky mua lien tuc) de demo dashboard / AI
# Dung Python thay cho mosquitto_pub vi PowerShell 5.1 lam mat dau " khi truyen JSON cho chuong trinh ngoai.

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pythonExe = Join-Path $baseDir ".venv\Scripts\python.exe"
if (-not (Test-Path $pythonExe)) { $pythonExe = "python" }

Write-Host "Mo phong cong1 + duong1 (Ctrl+C de dung)..." -ForegroundColor Cyan
& $pythonExe (Join-Path $baseDir "test_simulator.py") --loop @args
