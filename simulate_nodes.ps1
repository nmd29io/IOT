# PowerShell Script mo phong du lieu tu 2 Node ESP32 va Radar Thoi Tiet gui len MQTT
# Dung de test he thong, AI Multi-Horizon va Dashboard ma chua can cam mach that

$mosquitto_pub = "C:\Program Files\Mosquitto\mosquitto_pub.exe"

if (-not (Test-Path $mosquitto_pub)) {
    Write-Host "[ERROR] Khong tim thay mosquitto_pub tai $mosquitto_pub" -ForegroundColor Red
    exit 1
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   BAT DAU MO PHONG 2 NODE ESP32 & RADAR KHI TUONG (MQTT)" -ForegroundColor Cyan
Write-Host "   Ho tro kiem thu AI Multi-Horizon (5m, 15m, 30m, 60m)" -ForegroundColor Yellow
Write-Host "   Nhan Ctrl+C de dung mo phong" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Cyan

# Bao 2 node online
& $mosquitto_pub -h localhost -t "flood/cong1/online" -m "online" -r
& $mosquitto_pub -h localhost -t "flood/duong1/online" -m "online" -r
Write-Host "[INIT] Da phat trang thai online cho 2 node" -ForegroundColor Green

# Kich ban mo phong chu ky ngap lut
$t = 0
$level_cm = 40.0
$depth_cm = 0.0

while ($true) {
    $t += 1
    
    $rain_curr = 0.1
    $rain_30m_est = 3.0
    $river_level = 42.0

    if ($t -le 15) {
        # Giai doan 1: Binh thuong
        $level_cm = 40.0 + ($t * 2.0)
        $rain_curr = 0.2
        $rain_30m_est = 5.0
    } elseif ($t -le 35) {
        # Giai doan 2: Mua to bat ngo, nuoc cong dang nhanh
        $level_cm = 70.0 + (($t - 15) * 4.5)
        if ($level_cm -gt 165.0) { $level_cm = 165.0 }
        $rain_curr = 0.95
        $rain_30m_est = 25.0
        $river_level = 68.0
    } elseif ($t -le 50) {
        # Giai doan 3: Nuoc tran ra duong gay ngap
        $level_cm = 165.0 + (Get-Random -Minimum -2.0 -Maximum 2.0)
        $depth_cm = ($t - 35) * 1.5
        if ($depth_cm -gt 25.0) { $depth_cm = 25.0 }
        $rain_curr = 1.1
        $rain_30m_est = 35.0
        $river_level = 78.0
    } else {
        # Giai doan 4: Rut nuoc dan
        $level_cm = [Math]::Max(40.0, 165.0 - (($t - 50) * 5.0))
        $depth_cm = [Math]::Max(0.0, 25.0 - (($t - 50) * 1.2))
        $rain_curr = 0.05
        $rain_30m_est = 1.0
        $river_level = 45.0
        if ($level_cm -le 40.0 -and $depth_cm -le 0.0) {
            $t = 0
        }
    }

    $fill_pct = [Math]::Round(($level_cm / 180.0) * 100, 1)
    if ($fill_pct -gt 100.0) { $fill_pct = 100.0 }
    $level_rounded = [Math]::Round($level_cm, 1)
    $depth_rounded = [Math]::Round($depth_cm, 1)

    # 1. Publish du lieu cong1
    $json_cong1 = "{""level_cm"": $level_rounded, ""fill_pct"": $fill_pct, ""timestamp"": $([DateTimeOffset]::Now.ToUnixTimeSeconds())}"
    & $mosquitto_pub -h localhost -t "flood/cong1/level" -m "$json_cong1"

    # 2. Publish du lieu duong1
    $status_duong = "green"
    if ($depth_rounded -ge 15.0) {
        $status_duong = "red"
    } elseif ($depth_rounded -ge 5.0) {
        $status_duong = "yellow"
    }
    $json_duong1 = "{""depth_cm"": $depth_rounded, ""status"": ""$status_duong"", ""timestamp"": $([DateTimeOffset]::Now.ToUnixTimeSeconds())}"
    & $mosquitto_pub -h localhost -t "flood/duong1/depth" -m "$json_duong1"

    # 3. Publish du lieu Radar thoi tiet dinh ky (moi 2 chu ky)
    if ($t % 2 -eq 0) {
        $json_radar = "{""rain_current"": $rain_curr, ""rain_15m"": $($rain_30m_est * 0.4), ""rain_30m"": $rain_30m_est, ""rain_60m"": $($rain_30m_est * 1.6), ""downstream_river"": $river_level}"
        & $mosquitto_pub -h localhost -t "flood/weather/radar" -m "$json_radar"
    }

    Write-Host "[PUB] Cong1: $level_rounded cm ($fill_pct%) | Duong1: $depth_rounded cm ($status_duong) | Mua 30m: $rain_30m_est mm" -ForegroundColor White

    Start-Sleep -Seconds 3
}
