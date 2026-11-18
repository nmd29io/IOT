# start_cloud_tunnel.ps1 - PHUONG AN DU PHONG khi khong dung duoc Render:
# chay cloud server (broker MQTT tich hop) tren may nay + Cloudflare Tunnel de dien thoai truy cap qua HTTPS.
#
#   start_cloud_tunnel.bat            -> server + tunnel; in ra dia chi cho dien thoai va cho gateway
#   start_cloud_tunnel.bat -Local     -> nhu tren + chay luon cloud_link tren may nay (gateway va server cung laptop)
#
# Mat khau doc tu cloud\tunnel.env (khong commit). Chua co thi script tu tao ngau nhien.
# Luu y khi trinh bay: day la "server tu host, truy cap qua Cloudflare Tunnel", KHONG phai trien khai cloud.
param([switch]$Local, [int]$Port = 8080)

$base = Split-Path -Parent $MyInvocation.MyCommand.Path
$serverDir = Join-Path $base "cloud\server"
$envFile = Join-Path $base "cloud\tunnel.env"
$logFile = Join-Path $base "cloud\.tunnel.log"

Write-Host ""
Write-Host "=== CLOUD DU PHONG: server tren may nay + Cloudflare Tunnel ===" -ForegroundColor Cyan

# 1. Mat khau
function New-Secret { -join ((48..57) + (65..90) + (97..122) | Get-Random -Count 24 | ForEach-Object { [char]$_ }) }
if (-not (Test-Path $envFile)) {
    "# Mat khau cho start_cloud_tunnel (KHONG commit)`r`nADMIN_TOKEN=$(New-Secret)`r`nEDGE_PASSWORD=$(New-Secret)" | Set-Content -Encoding ASCII $envFile
    Write-Host "Da tao mat khau moi trong cloud\tunnel.env" -ForegroundColor Yellow
}
$cfg = @{}
foreach ($line in Get-Content $envFile) {
    if ($line -match '^\s*([A-Z_]+)\s*=\s*(.+?)\s*$') { $cfg[$Matches[1]] = $Matches[2] }
}
if (-not $cfg.ADMIN_TOKEN -or -not $cfg.EDGE_PASSWORD) { Write-Host "cloud\tunnel.env thieu ADMIN_TOKEN / EDGE_PASSWORD" -ForegroundColor Red; exit 1 }

# 2. Kiem tra Node.js, thu vien server, cong trong
if (-not (Get-Command node.exe -ErrorAction SilentlyContinue)) { Write-Host "Chua cai Node.js (>= 22.5)" -ForegroundColor Red; exit 1 }
if (-not (Test-Path (Join-Path $serverDir "node_modules\aedes"))) {
    Write-Host "Cai thu vien cho cloud server..." -ForegroundColor Yellow
    & npm.cmd install --prefix "$serverDir" --no-audit --no-fund
}
if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) {
    Write-Host "Cong $Port dang bi chiem (co the server cu dang chay). Dong cua so do roi chay lai, hoac dung -Port 8090." -ForegroundColor Red
    exit 1
}

# 3. cloudflared
function Find-Cloudflared {
    $c = Get-Command cloudflared.exe -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in @("$env:ProgramFiles\cloudflared\cloudflared.exe", "${env:ProgramFiles(x86)}\cloudflared\cloudflared.exe",
                     "$env:LOCALAPPDATA\Microsoft\WinGet\Links\cloudflared.exe")) { if (Test-Path $p) { return $p } }
    return $null
}
$cf = Find-Cloudflared
if (-not $cf) {
    Write-Host "Chua co cloudflared -> cai bang winget..." -ForegroundColor Yellow
    & winget install --id Cloudflare.cloudflared -e --accept-source-agreements --accept-package-agreements
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    $cf = Find-Cloudflared
    if (-not $cf) { Write-Host "Khong cai duoc cloudflared. Tai tai: github.com/cloudflare/cloudflared/releases" -ForegroundColor Red; exit 1 }
}

# 4. Cloud server (che do broker tich hop) - cua so rieng
$env:BROKER = "embedded"; $env:PORT = "$Port"
$env:ADMIN_TOKEN = $cfg.ADMIN_TOKEN; $env:EDGE_PASSWORD = $cfg.EDGE_PASSWORD
$env:DB_PATH = Join-Path $serverDir "data\flood-tunnel.db"
Write-Host "[1] Khoi dong cloud server (cong $Port)..." -ForegroundColor Green
Start-Process -FilePath "node.exe" -ArgumentList "--no-warnings", "server.js" -WorkingDirectory $serverDir
$ok = $false
for ($i = 0; $i -lt 20 -and -not $ok; $i++) {
    Start-Sleep -Milliseconds 500
    try { $ok = (Invoke-RestMethod "http://localhost:$Port/healthz" -TimeoutSec 2).broker -eq "embedded" } catch { }
}
if (-not $ok) { Write-Host "Server khong khoi dong duoc - xem cua so node" -ForegroundColor Red; exit 1 }

# 5. Tunnel - cua so rieng, doc dia chi tu file log
if (Test-Path $logFile) { Remove-Item $logFile -Force }
Write-Host "[2] Mo Cloudflare Tunnel..." -ForegroundColor Green
Start-Process -FilePath $cf -ArgumentList "tunnel", "--no-autoupdate", "--url", "http://localhost:$Port", "--logfile", "`"$logFile`""
$url = $null
for ($i = 0; $i -lt 60 -and -not $url; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $logFile) {
        $m = Select-String -Path $logFile -Pattern 'https://[a-z0-9-]+\.trycloudflare\.com' | Select-Object -First 1
        if ($m) { $url = $m.Matches[0].Value }
    }
}
if (-not $url) { Write-Host "Chua lay duoc dia chi tunnel - xem cua so cloudflared / file cloud\.tunnel.log" -ForegroundColor Red; exit 1 }
$wss = $url -replace '^https://', 'wss://'

# 6. Gateway tren cung may (tuy chon): noi thang localhost, khong can di vong qua tunnel
if ($Local) {
    Write-Host "[3] Khoi dong cloud_link (gateway -> server tren may nay)..." -ForegroundColor Green
    $linkDir = Join-Path $base "gateway\cloud_link"
    if (-not (Test-Path (Join-Path $linkDir "node_modules\mqtt"))) { & npm.cmd install --prefix "$linkDir" --no-audit --no-fund }
    $env:CLOUD_URL = "ws://localhost:$Port/mqtt"
    Start-Process -FilePath "node.exe" -ArgumentList "cloud_link.js" -WorkingDirectory $linkDir
}

$ip = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
       Where-Object { $_.InterfaceAlias -match 'Wi-?Fi' -and $_.IPAddress -notlike '169.*' } | Select-Object -First 1).IPAddress
Write-Host ""
Write-Host "==================== DIA CHI ====================" -ForegroundColor Cyan
Write-Host " Dien thoai (cong khai) : $url" -ForegroundColor White
Write-Host " Dien thoai (quan tri)  : $url/admin" -ForegroundColor White
Write-Host " Ma quan tri            : (ADMIN_TOKEN trong cloud\tunnel.env)" -ForegroundColor White
Write-Host ""
if (-not $Local) {
    Write-Host " Laptop gateway: sua gateway\cloud_link\config.json roi chay start_all.bat" -ForegroundColor Yellow
    Write-Host "   cung hotspot : `"url`": `"ws://${ip}:$Port/mqtt`"   (on dinh hon)" -ForegroundColor White
    Write-Host "   qua Internet : `"url`": `"$wss/mqtt`"" -ForegroundColor White
    Write-Host "   `"password`"   : EDGE_PASSWORD trong cloud\tunnel.env" -ForegroundColor White
    Write-Host "   (Windows Firewall co the hoi cho phep Node.js - chon Allow cho mang Private)" -ForegroundColor DarkGray
}
Write-Host " Dia chi tunnel DOI moi lan chay: cai lai app + bat lai thong bao tren dien thoai." -ForegroundColor DarkGray
Write-Host " Dung: dong cac cua so node va cloudflared." -ForegroundColor DarkGray
Write-Host "=================================================" -ForegroundColor Cyan
