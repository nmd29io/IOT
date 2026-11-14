# start_all.ps1 — Khoi dong toan bo he thong Flood Monitor
# Chay: Mosquitto Broker + Node-RED (Edge IT) + Edge ML Service (AI Predictor)

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   HE THONG GIAM SAT VA CANH BAO NGAP UNG DO THI" -ForegroundColor Cyan
Write-Host "   Khoi dong MQTT Broker, AI ML Engine va Dashboard..." -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 1. Khoi dong Mosquitto MQTT Broker
$mosquittoExe = "C:\Program Files\Mosquitto\mosquitto.exe"
$mosquittoConf = Join-Path $baseDir "mosquitto\mosquitto.conf"

if (Test-Path $mosquittoExe) {
    Write-Host "[1/3] Khoi dong Mosquitto MQTT Broker (port 1883)..." -ForegroundColor Green
    Start-Process -FilePath $mosquittoExe -ArgumentList "-v", "-c", "`"$mosquittoConf`""
} else {
    Write-Host "[ERROR] Khong tim thay mosquitto.exe tai $mosquittoExe" -ForegroundColor Red
}

Start-Sleep -Seconds 2

# 2. Khoi dong Edge ML Service (Python AI Predictor)
$pythonExe = Join-Path $baseDir ".venv\Scripts\python.exe"
$mlScript = Join-Path $baseDir "ml\edge_ml_service.py"

if (Test-Path $pythonExe) {
    Write-Host "[2/3] Khoi dong Edge ML AI Service (Multi-Horizon RF + Manning/GIS Engine)..." -ForegroundColor Green
    Start-Process -FilePath $pythonExe -ArgumentList "-u", "`"$mlScript`""
} else {
    Write-Host "[WARN] Khong tim thay Python venv, bo qua khoi dong ML Service" -ForegroundColor Yellow
}

Start-Sleep -Seconds 2

# 3. Khoi dong Node-RED
$nodeRedCmd = "C:\Users\Duc\AppData\Local\pnpm\node-red.cmd"
$userDir = Join-Path $baseDir "nodered\.node-red"

if (Test-Path $nodeRedCmd) {
    Write-Host "[3/3] Khoi dong Node-RED Edge IT (port 1880)..." -ForegroundColor Green
    Start-Process -FilePath $nodeRedCmd -ArgumentList "--userDir", "`"$userDir`""
} else {
    Write-Host "[ERROR] Khong tim thay node-red tai $nodeRedCmd" -ForegroundColor Red
}

Start-Sleep -Seconds 5

# 4. Mo Dashboard tren trinh duyet
Write-Host ""
Write-Host "Dang mo Dashboard tai http://localhost:1880/ui ..." -ForegroundColor Magenta
Start-Process "http://localhost:1880/ui"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   HE THONG DA KHOI DONG THANH CONG!" -ForegroundColor Green
Write-Host "   - Dashboard giam sat: http://localhost:1880/ui" -ForegroundColor White
Write-Host "   - Node-RED Flow Editor: http://localhost:1880" -ForegroundColor White
Write-Host "   - MQTT Broker: localhost:1883" -ForegroundColor White
Write-Host "   - AI Engine: Dang du bao muc nuoc da tang +5m, +15m, +30m, +60m tich hop Manning & GIS" -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""
