# Chạy firmware trên Wokwi (mô phỏng ESP32 + 2 cảm biến + đèn + còi)

```
Wokwi ESP32 (2 node logic cong1 + duong1)
      │ WiFi "Wokwi-GUEST" → Internet
      ▼
broker.hivemq.com   (topic có tiền tố riêng: uet-int3323-n1-09e061/flood/...)
      ▲ MQTT bridge (mosquitto/conf.d/wokwi-bridge.conf)
      │
Laptop: Mosquitto ──▶ Node-RED ──▶ Dashboard / Telegram / Cloud   (không phải sửa gì)
```

ESP32 trên Wokwi **không thấy laptop** (`localhost`, `10.x.x.x` đều không dùng được) nên phải đi qua một broker công cộng; Mosquitto trên laptop bridge broker đó về.

## 1. Laptop: bật bridge
```bat
copy mosquitto\conf.d\wokwi-bridge.conf.example mosquitto\conf.d\wokwi-bridge.conf
```
Đóng cửa sổ Mosquitto đang chạy (nếu có) rồi chạy lại `start_all.bat`.

## 2. Wokwi: tạo project
1. Vào https://wokwi.com → **New Project → ESP32** (Arduino).
2. Tab `sketch.ino`: xoá hết, dán nội dung [`firmware/flood_node/flood_node.ino`](../flood_node/flood_node.ino).
3. Tab `diagram.json`: dán [`diagram.json`](diagram.json).
4. Bấm mũi tên cạnh các tab → **New file…**:
   - `config.h` ← dán [`config.wokwi.h`](config.wokwi.h)
   - `libraries.txt` ← dán [`libraries.txt`](libraries.txt)
5. Bấm ▶ **Start**. Serial Monitor phải hiện:
   ```
   [WiFi] IP: 10.10.0.2
   [MQTT] cong1 đã kết nối broker
   [MQTT] duong1 đã kết nối broker
   ```
   Dashboard (http://localhost:1880/ui) chuyển sang **ONLINE** cho cả hai node.

## 3. Thử các kịch bản
Khi đang chạy, **bấm vào cảm biến** để hiện thanh trượt khoảng cách:

| Cảm biến | Khoảng cách | Kết quả |
|---|---|---|
| Trên – cống (H = 60, Hmax = 35) | 60 → 40 cm | L ≈ 57% → **VÀNG** |
| | kéo nhanh xuống 35 cm (dâng ≥ 5 cm/phút) hoặc 30 cm | **ĐỎ** → Telegram yêu cầu bật bơm/mở van |
| Dưới – đường (d0 = 50) | 38 cm | ngập 12 cm → đèn **vàng** sau 5 s |
| | 25 cm | ngập 25 cm → đèn **đỏ** + còi |
| | kéo về 25 cm chỉ 2 giây rồi trả lại | **không đổi mức** (lọc xe/người đi qua) |

Thử thêm từ dashboard: **Tắt còi**, form cấu hình, **Hiệu chuẩn** (đặt khoảng cách rồi bấm — H/d0 lấy theo giá trị đang đo).
Bấm ⏹ Stop trên Wokwi → khoảng 1 phút sau dashboard báo node **OFFLINE** (Last Will).

## Lưu ý
- Wokwi dùng **HC-SR04**, cùng giao thức với JSN-SR04T; vùng mù 20 cm vẫn được firmware áp dụng.
- `stable_s` đặt 5 s cho dễ thử; mạch thật dùng 20 s.
- Broker công cộng **không có mật khẩu**: chỉ dùng thử nghiệm. Tiền tố ngẫu nhiên giúp không trùng nhóm khác.
- Chuyển sang mạch thật: xoá `mosquitto\conf.d\wokwi-bridge.conf`, dùng `config.example.h` với IP laptop.
- Khi bảo vệ, nói rõ đây là **mô phỏng**; phần cứng thật vẫn là sản phẩm chính.
