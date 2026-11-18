# start_all.ps1 - Khoi dong toan bo he thong Flood Monitor tren may Edge (laptop)
# Mosquitto Broker + Edge ML Service + Node-RED (Edge IT + Dashboard)
# Canh bao den dien thoai: Web Push tu cloud cua nhom (xem cloud/README.md).

$baseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$problems = @()

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   HE THONG IOT GIAM SAT VA CANH BAO NGAP UNG DO THI" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Mosquitto (chay trong thu muc mosquitto\ de duong dan tuong doi trong mosquitto.conf dung)
$mosquittoExe = $null
foreach ($c in @("C:\Program Files\Mosquitto\mosquitto.exe", "C:\Program Files (x86)\Mosquitto\mosquitto.exe")) {
    if (Test-Path $c) { $mosquittoExe = $c; break }
}
if (-not $mosquittoExe) { $mosquittoExe = (Get-Command mosquitto -ErrorAction SilentlyContinue).Source }
$mosquittoDir = Join-Path $baseDir "mosquitto"
New-Item -ItemType Directory -Force (Join-Path $mosquittoDir "data") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $mosquittoDir "conf.d") | Out-Null
if ($mosquittoExe) {
    Write-Host "[1/3] Khoi dong Mosquitto (port 1883)..." -ForegroundColor Green
    Start-Process -FilePath $mosquittoExe -ArgumentList "-v", "-c", "mosquitto.conf" -WorkingDirectory $mosquittoDir
    Start-Sleep -Seconds 2
} else {
    Write-Host "[1/3] LOI: chua cai Mosquitto" -ForegroundColor Red
    $problems += "Cai Mosquitto: winget install EclipseFoundation.Mosquitto  (hoac tai tu https://mosquitto.org/download/)"
}

# 2. Edge ML Service: can Python co paho-mqtt, pandas, numpy, scikit-learn, joblib
$pythonExe = Join-Path $baseDir ".venv\Scripts\python.exe"
if (-not (Test-Path $pythonExe)) { $pythonExe = $null }
if ($pythonExe) {
    & $pythonExe -c "import paho.mqtt, pandas, numpy, sklearn, joblib" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[2/3] Khoi dong Edge ML Service..." -ForegroundColor Green
        Start-Process -FilePath $pythonExe -ArgumentList "-u", "`"$(Join-Path $baseDir 'ml\edge_ml_service.py')`""
    } else {
        Write-Host "[2/3] BO QUA ML: .venv thieu thu vien" -ForegroundColor Yellow
        $problems += "Cai thu vien ML: .venv\Scripts\pip install -r ml\requirements.txt"
    }
} else {
    Write-Host "[2/3] BO QUA ML: chua co .venv (khong anh huong canh bao chinh)" -ForegroundColor Yellow
    $problems += "Tao moi truong Python cho ML: py -3.11 -m venv .venv ; .venv\Scripts\pip install -r ml\requirements.txt"
}

# 3. Node-RED
$userDir = Join-Path $baseDir "nodered\.node-red"
if (-not (Get-Command npm.cmd -ErrorAction SilentlyContinue)) {
    Write-Host "[3/3] LOI: chua cai Node.js" -ForegroundColor Red
    $problems += "Cai Node.js LTS: https://nodejs.org"
} else {
    # Palette node-red-dashboard nam trong userDir\node_modules
    if (-not (Test-Path (Join-Path $userDir "node_modules\node-red-dashboard"))) {
        Write-Host "[3/3] Cai palette Node-RED lan dau (1-2 phut)..." -ForegroundColor Yellow
        & npm.cmd install --prefix "$userDir" --no-audit --no-fund
    }
    $nodeRed = (Get-Command node-red.cmd -ErrorAction SilentlyContinue).Source
    if (-not $nodeRed) {
        foreach ($c in @("$env:APPDATA\npm\node-red.cmd", "$env:LOCALAPPDATA\pnpm\node-red.cmd")) {
            if (Test-Path $c) { $nodeRed = $c; break }
        }
    }
    Write-Host "[3/3] Khoi dong Node-RED (port 1880)..." -ForegroundColor Green
    if ($nodeRed) {
        Start-Process -FilePath $nodeRed -ArgumentList "--userDir", "`"$userDir`""
    } else {
        # Goi npx.cmd (KHONG goi "npx": PowerShell se mo npx.ps1 bang trinh soan thao thay vi chay)
        Write-Host "      (chua cai node-red global -> chay qua npx, lan dau se tai ve)" -ForegroundColor DarkGray
        Start-Process -FilePath "npx.cmd" -ArgumentList "--yes", "node-red@4", "--userDir", "`"$userDir`""
    }
    Start-Sleep -Seconds 8
    Start-Process "http://localhost:1880/ui"
}

# 4. Gateway -> Cloud qua wss (Render.com hoac start_cloud_tunnel.bat). Chi chay khi da tao gateway\cloud_link\config.json
#    (dung VM + Mosquitto bridge thi khong can buoc nay: xem mosquitto\conf.d\cloud-bridge.conf.example)
$linkDir = Join-Path $baseDir "gateway\cloud_link"
if (Test-Path (Join-Path $linkDir "config.json")) {
    if (-not (Test-Path (Join-Path $linkDir "node_modules\mqtt"))) {
        Write-Host "[+] Cai thu vien cho cloud_link lan dau..." -ForegroundColor Yellow
        & npm.cmd install --prefix "$linkDir" --no-audit --no-fund
    }
    Write-Host "[+] Khoi dong ket noi Gateway -> Cloud (cloud_link)..." -ForegroundColor Green
    Start-Process -FilePath "node.exe" -ArgumentList "`"$(Join-Path $linkDir 'cloud_link.js')`"" -WorkingDirectory $linkDir
} else {
    Write-Host "[+] Chua noi cloud qua wss (khong co gateway\cloud_link\config.json)" -ForegroundColor DarkGray
}

Write-Host ""
if ($problems.Count -gt 0) {
    Write-Host "Can xu ly truoc khi demo:" -ForegroundColor Yellow
    $problems | ForEach-Object { Write-Host "   - $_" -ForegroundColor Yellow }
    Write-Host ""
}
Write-Host "   Dashboard : http://localhost:1880/ui   (tab 'Quan ly thiet bi' de cau hinh tu xa)" -ForegroundColor White
Write-Host "   Flow      : http://localhost:1880" -ForegroundColor White
Write-Host "   Broker    : localhost:1883  |  Kiem tra: mosquitto_sub -t `"flood/#`" -v" -ForegroundColor White
Write-Host "   Mo phong  : simulate_nodes.bat (khi chua co mach that)" -ForegroundColor White
Write-Host ""
