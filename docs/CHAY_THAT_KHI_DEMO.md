# Chạy thật khi demo (mạch ESP32 thật, không mô phỏng)

So với mô phỏng, phía laptop và cloud **giữ nguyên**. Chỉ khác: ESP32 thật nối **thẳng vào laptop** qua cùng một WiFi
(không đi qua broker công cộng như Wokwi).

```
Mô phỏng:  Wokwi ESP32 ─Internet─▶ broker.hivemq.com ─bridge─▶ Laptop (Mosquitto + Node-RED) ─▶ Cloud ─▶ Điện thoại
Chạy thật: ESP32 thật ──WiFi hotspot──────────────────────────▶ Laptop (Mosquitto + Node-RED) ─▶ Cloud ─▶ Điện thoại
```

---

## A. Chuẩn bị (làm một lần, trước buổi demo vài ngày)

### A1. Mạng — dùng hotspot điện thoại
WiFi trường / quán thường chặn các thiết bị nói chuyện với nhau hoặc bắt đăng nhập qua trang web (ESP32 không làm được).

- [ ] Bật hotspot ở **2,4 GHz** (ESP32 không bắt được 5 GHz). Ghi lại tên và mật khẩu.
- [ ] Cho laptop vào hotspot → chạy `ipconfig` → ghi **IPv4** của card **Wi-Fi** (VD `192.168.43.120`).
      Bỏ qua các địa chỉ `192.168.x.1` của card **VMware**.
- [ ] Mỗi lần dùng cùng hotspot, laptop thường nhận lại đúng IP này. Kiểm tra lại trước giờ demo.

### A2. Phần cứng — Đức
- [ ] Lắp theo README mục 4: cảm biến **cống** GPIO 5/18 · cảm biến **đường** GPIO 19/21 · LED xanh/vàng/đỏ GPIO 26/27/14 (qua điện trở 220 Ω) · còi GPIO 25.
- [ ] **Cầu chia áp 1 kΩ + 2 kΩ ở chân ECHO của CẢ HAI cảm biến** (ECHO ra 5V, ESP32 chỉ chịu 3,3V — nối thẳng có thể hỏng chân).
- [ ] Cảm biến cống gắn ở miệng ống, cảm biến đường trên giá phía trên khay, cả hai chiếu **vuông góc** xuống mặt nước.
- [ ] Mỗi cảm biến cách **mặt nước cao nhất ≥ 20 cm** (vùng mù của JSN-SR04T).
- [ ] Đặt hai cảm biến cách xa nhau, không cùng chiếu vào một mặt nước.

### A3. Nạp firmware thật
- [ ] Sao chép `firmware/flood_node/config.example.h` → `firmware/flood_node/config.h` và điền:
  ```cpp
  #define NODE_MODE     MODE_DUAL
  #define WIFI_SSID     "Ten_hotspot"
  #define WIFI_PASSWORD "mat_khau_hotspot"
  #define MQTT_SERVER   "192.168.43.120"   // IPv4 của laptop ở bước A1
  ```
  **Không** đặt `TOPIC_PREFIX` (để trống). Không dùng `config.wokwi.h` cho mạch thật.
- [ ] Arduino IDE → board **ESP32 Dev Module** → cài thư viện **PubSubClient** và **ArduinoJson (≥ 7)** → Upload.
- [ ] Serial Monitor 115200 phải hiện:
  ```
  [WiFi] IP: 192.168.43.xx
  [MQTT] cong1 đã kết nối broker
  [MQTT] duong1 đã kết nối broker
  ```
  Báo `rc=-2` → sai IP laptop, Mosquitto chưa chạy, hoặc hotspot chặn thiết bị.

### A4. Laptop — tắt bridge Wokwi
- [ ] Xoá (hoặc đổi tên) file bridge của mô phỏng, rồi chạy lại hệ thống:
  ```bat
  del mosquitto\conf.d\wokwi-bridge.conf
  start_all.bat
  ```
  Nếu còn file này, dữ liệu ảo từ Wokwi có thể lẫn vào dữ liệu thật.

### A5. Cloud và app điện thoại — Vân Anh
- [ ] Dựng máy ảo theo [cloud/README.md](../cloud/README.md) → `docker compose up -d --build`.
- [ ] Laptop: sao chép `mosquitto\conf.d\cloud-bridge.conf.example` → `cloud-bridge.conf`, điền IP máy ảo + `EDGE_PASSWORD`, chạy lại `start_all.bat`.
- [ ] Điện thoại người quản lý: mở `https://<IP>.sslip.io/admin` → đăng nhập → **📲 Cài ứng dụng** → **🔔 Bật thông báo** → **Gửi thử** (phải rung).
      iPhone: Safari → Chia sẻ → *Thêm vào MH chính*, mở app từ màn hình chính rồi mới bật thông báo (iOS 16.4+).

### A6. Hiệu chuẩn trên mô hình thật
Trên tab **Quản lý thiết bị** (dashboard laptop hoặc app):
- [ ] Ống **rỗng** → bấm **Hiệu chuẩn cong1** (node đo lại H).
- [ ] Khay **khô** → bấm **Hiệu chuẩn duong1** (node đo lại d0).
- [ ] Đo bằng thước chiều sâu ống dùng để tính % → nhập **Hmax**.
- [ ] Ngưỡng đường quy đổi cho khay nông, ví dụ: VÀNG 2 cm · ĐỎ 4 cm · ĐỎ→VÀNG khi dưới 3 cm · VÀNG→XANH khi dưới 1,5 cm.
- [ ] Thời gian ổn định: 20 s (thật) — có thể hạ 10 s cho demo nhanh hơn.
- [ ] Đổ thử một lần đủ các kịch bản ở mục B2; ghi số liệu cho phần **Thực nghiệm** của báo cáo (HUONG_DAN_DEMO mục 8).

---

## B. Trong buổi demo

### B1. Khởi động (5 phút trước giờ)
- [ ] Bật hotspot → cắm nguồn ESP32 → chạy `start_all.bat` trên laptop.
- [ ] Dashboard laptop (`http://localhost:1880/ui`): cả `cong1` và `duong1` **ONLINE**.
- [ ] App điện thoại: **Gateway online**, thấy số đo cập nhật.
- [ ] Ống rỗng, khay khô → tất cả **XANH**.

### B2. Kịch bản (chi tiết lời thoại: HUONG_DAN_DEMO mục 4)
| # | Làm | Thấy |
|---|---|---|
| 1 | Đổ nước từ từ rồi nhanh vào ống | Cống VÀNG (do **tốc độ dâng**) → ĐỎ → **điện thoại rung**: "YÊU CẦU bật bơm / mở van" |
| 2 | Cho nước tràn sang khay | Sau thời gian ổn định: đèn vàng/đỏ, còi kêu → "🌊 Ngập đang lan rộng – cống báo trước X phút" |
| 3 | Bấm **✅ Đã xử lý** ngay trên thông báo; bấm **Tắt còi** trong app | App ghi người xác nhận và thời gian phản hồi; còi tắt |
| 4 | Đưa tay qua cảm biến đường 2–3 giây | **Không** đổi màu đèn (chống báo sai) |
| 5 | Tắt hotspot khi đang ngập | **Đèn và còi vẫn chạy**; app báo mất kết nối. Bật lại hotspot → tự kết nối lại |
| 6 | Đổi ngưỡng / hiệu chuẩn từ app trên 4G | Nhật ký: "duong1: đã áp dụng cấu hình mới" |
| 7 | Mở `https://<IP>.sslip.io` trên điện thoại giám khảo | Bản đồ ngập công khai |

### B3. Kết thúc
- [ ] Rút nước → hệ thống về XANH, app có **tổng kết sự cố** (kéo dài bao lâu, ai xác nhận, sau bao lâu).
- [ ] Mở **Thống kê 7 ngày** trên app: số sự cố, thời gian phản hồi, thời gian báo trước.

---

## C. Khác với lúc mô phỏng

| | Mô phỏng (Wokwi) | Chạy thật |
|---|---|---|
| ESP32 kết nối tới | `broker.hivemq.com` + bridge | IP laptop, trực tiếp |
| `config.h` | `config.wokwi.h` (có `TOPIC_PREFIX`) | `config.example.h` (không có tiền tố) |
| `mosquitto/conf.d/wokwi-bridge.conf` | Bật | **Xoá** |
| Thời gian ổn định | 5 s | 20 s (hoặc 10 s khi demo) |
| Báo offline khi rút điện ESP32 | ~1 phút | ~15–20 s (keep-alive MQTT) |
| Số liệu | Ảo — chỉ để thử | **Dùng cho phần thực nghiệm** |

---

## D. Sự cố thường gặp

| Hiện tượng | Xử lý |
|---|---|
| Serial báo `rc=-2` | Sai IP trong `config.h` · Mosquitto chưa chạy · hotspot chặn thiết bị → đổi hotspot khác |
| ESP32 không vào được WiFi | Hotspot đang ở 5 GHz → chuyển 2,4 GHz; kiểm tra tên/mật khẩu |
| ESP32 khởi động lại liên tục / không boot | Còi đang nối GPIO 12 → chuyển sang GPIO 25; kiểm tra nguồn USB |
| Số đo nhảy lung tung hoặc "lỗi đo" tăng | Cảm biến lệch góc, gần hơn 20 cm, thiếu cầu chia áp, dây ECHO lỏng |
| Đèn đổi màu chậm | Do thời gian ổn định (20 s) — chủ ý để chống báo sai; hạ xuống 10 s khi demo |
| Dashboard không có số liệu | `mosquitto_sub -t "flood/#" -v` xem node có gửi không; kiểm tra đã xoá `wokwi-bridge.conf` |
| Điện thoại không nhận thông báo | Phải mở qua **https**; đã bật thông báo; gateway đang online (bridge cloud chạy) |
| Mất Internet tại phòng demo | ESP32 + laptop + đèn/còi + dashboard cục bộ vẫn chạy đủ; chỉ mất cloud và thông báo đẩy → dùng video quay sẵn phần app |

## E. Phương án dự phòng
- **Phần cứng hỏng**: `simulate_nodes.bat` hoặc Wokwi ([firmware/wokwi/README.md](../firmware/wokwi/README.md)) — nói rõ là mô phỏng.
- **Quay sẵn video** toàn bộ kịch bản B2 trước buổi demo (cũng là video nộp bài 5–7 phút).
