# Node-RED — Gateway / Edge IT

Node-RED chạy trên laptop (gateway), cùng Mosquitto. Toàn bộ logic xử lý là **function node do nhóm tự viết**;
palette duy nhất cần cài là `node-red-dashboard` (dashboard cục bộ cho người vận hành tại trạm).

## 1. Chạy

Cách nhanh nhất: `start_all.bat` ở thư mục gốc. Script tự cài palette vào `nodered/.node-red`, khởi động Node-RED
với userDir đó (dùng sẵn `flows.json`, không cần import) rồi mở http://localhost:1880/ui.

Chạy tay:
```bash
npm install -g --unsafe-perm node-red
cd nodered/.node-red && npm install
node-red --userDir nodered/.node-red
```

| Phần mềm | Phiên bản |
|---|---|
| Node.js | ≥ 18 LTS |
| Node-RED | 4.x |
| node-red-dashboard | 3.6.x |

Broker mặc định `localhost:1883` (node cấu hình **Flood MQTT Broker**).

## 2. Dashboard cục bộ (http://localhost:1880/ui)

**Tab "Flood Monitor"**
- Cống 1: độ đầy L, mức cảnh báo, mực nước, tốc độ dâng (cửa sổ 1 phút), kết nối
- Đường 1: độ sâu ngập, mức đèn tại chỗ, còi, kết nối, nút 🔇 Tắt còi / 🔔 Bật lại còi
- Cảnh báo & yêu cầu vận hành: tình trạng chung, yêu cầu bật bơm/mở van đang chờ, nút ✅ Xác nhận, nhật ký; cảnh báo quan trọng hiện thành thông báo nổi
- Dự báo AI, biểu đồ lịch sử

**Tab "Quản lý thiết bị"**
- Bảng thiết bị (online/offline, mức, số đo, RSSI, uptime, lỗi đo, firmware) – node mới tự xuất hiện
- Form cấu hình từ xa cho cong1 / duong1 (publish retained `flood/<id>/config`, node xác nhận qua `flood/<id>/event`)
- Nút hiệu chuẩn: đo lại H (cống rỗng) / d0 (đường khô)

Người quản lý ở xa dùng **app quản lý trên cloud** (PWA, nhận thông báo đẩy) — xem `cloud/README.md`.

## 3. Cấu trúc flow

```
flood/+/level  → Định tuyến (có L,m = cống | có h = đường)
                   ├→ Node cống: TB trượt 5 mẫu, v theo 1 phút, hysteresis + giữ 30 s ─┐
                   └→ Node đường: lấy mức do ESP32 tự phân ───────────────────────────┤
flood/+/status (Last Will) ─────────────────────────────────────────────────────────────┤
Inject 30 s (nhắc lại 10 phút, watchdog 30 s) ──────────────────────────────────────────┤
Nút Xác nhận dashboard / lệnh từ cloud flood/cloud/# (đã kiểm duyệt) ───────────────────┤
                                                                                        ▼
                                Bộ quyết định cảnh báo & sự cố ─┬→ flood/edge/alert    (cloud → Web Push điện thoại)
                                                                ├→ flood/edge/state    (retained, trạng thái tổng hợp)
                                                                ├→ flood/edge/incident (sự cố đã đóng → thống kê)
                                                                └→ dashboard, nhật ký → flood/edge/log
flood/+/info   → bảng thiết bị, điền form cấu hình
flood/+/event  → nhật ký; "calibrated" → cập nhật lại config retained
Form cấu hình  → kiểm tra → flood/<id>/config (retained)
Nút còi / hiệu chuẩn → flood/duong1/alarm/cmd, flood/<id>/cmd
flood/ai/prediction → dashboard AI; cảnh báo sớm → flood/edge/alert
```

## 4. Troubleshooting

| Vấn đề | Giải pháp |
|---|---|
| Node MQTT "disconnected" | Kiểm tra Mosquitto đang chạy (`start_all.bat`), cổng 1883 |
| `/ui` báo "Cannot GET" | Chưa cài palette: `cd nodered/.node-red && npm install`, khởi động lại Node-RED |
| Cổng 1880 đang bận | Đã có một Node-RED khác chạy — đóng cửa sổ đó rồi chạy lại |
| Không thấy dữ liệu | `mosquitto_sub -t "flood/#" -v` để xem node có publish không; hoặc chạy `simulate_nodes.bat` |
