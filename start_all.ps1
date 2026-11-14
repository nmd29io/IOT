# start_all.ps1 - Khoi dong toan bo he thong Flood Monitor tren may Edge (laptop)
# Mosquitto Broker + Edge ML Service + Node-RED (Edge IT + Dashboard)
# Bi mat (Telegram chat id, ThingSpeak key) doc tu config\secrets.env (khong commit).

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   HE THONG IOT GIAM SAT VA CANH BAO NGAP UNG DO THI" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 0. Nap bien moi truong tu config\secrets.env (dang KEY=VALUE)
$secrets = Join-Path $baseDir "config\secrets.env"
if (Test-Path $secrets) {
    Get-Content $secrets | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#") -and $line.Contains("=")) {
            $k, $v = $line.Split("=", 2)
            [Environment]::SetEnvironmentVariable($k.Trim(), $v.Trim(), "Process")
        }
    }
    Write-Host "[0/3] Da nap config\secrets.env" -ForegroundColor Green
} else {
    Write-Host "[0/3] Chua co config\secrets.env (sao chep tu secrets.env.example) - Telegram/ThingSpeak se bi tat" -ForegroundColor Yellow
}

# 1. Mosquitto (chay trong thu muc mosquitto\ de duong dan 'data/' trong mosquitto.conf dung tren moi may)
$mosquittoExe = "C:\Program Files\Mosquitto\mosquitto.exe"
$mosquittoDir = Join-Path $baseDir "mosquitto"
New-Item -ItemType Directory -Force (Join-Path $mosquittoDir "data") | Out-Null
if (Test-Path $mosquittoExe) {
    Write-Host "[1/3] Khoi dong Mosquitto (port 1883)..." -ForegroundColor Green
    Start-Process -FilePath $mosquittoExe -ArgumentList "-v", "-c", "mosquitto.conf" -WorkingDirectory $mosquittoDir
} else {
    Write-Host "[ERROR] Khong tim thay $mosquittoExe" -ForegroundColor Red
}
Start-Sleep -Seconds 2

# 2. Edge ML Service
$pythonExe = Join-Path $baseDir ".venv\Scripts\python.exe"
if (-not (Test-Path $pythonExe)) { $pythonExe = (Get-Command python -ErrorAction SilentlyContinue).Source }
if ($pythonExe) {
    Write-Host "[2/3] Khoi dong Edge ML Service..." -ForegroundColor Green
    Start-Process -FilePath $pythonExe -ArgumentList "-u", "`"$(Join-Path $baseDir 'ml\edge_ml_service.py')`""
} else {
    Write-Host "[WARN] Khong tim thay Python, bo qua ML Service" -ForegroundColor Yellow
}
Start-Sleep -Seconds 2

# 3. Node-RED: tim lenh node-red (npm/pnpm global), neu khong co thi dung npx
$userDir = Join-Path $baseDir "nodered\.node-red"
$nodeRed = (Get-Command node-red -ErrorAction SilentlyContinue).Source
if (-not $nodeRed) {
    foreach ($c in @("$env:APPDATA\npm\node-red.cmd", "$env:LOCALAPPDATA\pnpm\node-red.cmd")) {
        if (Test-Path $c) { $nodeRed = $c; break }
    }
}
Write-Host "[3/3] Khoi dong Node-RED (port 1880)..." -ForegroundColor Green
if ($nodeRed) {
    Start-Process -FilePath $nodeRed -ArgumentList "--userDir", "`"$userDir`""
} else {
    Start-Process -FilePath "npx" -ArgumentList "--yes", "node-red", "--userDir", "`"$userDir`""
}
Start-Sleep -Seconds 6
Start-Process "http://localhost:1880/ui"

Write-Host ""
Write-Host "   Dashboard : http://localhost:1880/ui   (tab 'Quan ly thiet bi' de cau hinh tu xa)" -ForegroundColor White
Write-Host "   Flow      : http://localhost:1880" -ForegroundColor White
Write-Host "   Broker    : localhost:1883  |  Kiem tra: mosquitto_sub -t `"flood/#`" -v" -ForegroundColor White
Write-Host "   Mo phong  : simulate_nodes.bat (khi chua co mach that)" -ForegroundColor White
Write-Host ""
