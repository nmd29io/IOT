# Node-RED Setup — Hệ thống Giám sát Ngập úng

## 1. Yêu cầu tiên quyết

| Phần mềm | Phiên bản | Ghi chú |
|---|---|---|
| Node.js | ≥ 18 LTS | https://nodejs.org |
| Node-RED | ≥ 3.1 | `npm install -g --unsafe-perm node-red` |
| node-red-dashboard | latest | Cài qua Palette Manager |
| node-red-contrib-telegrambot | ≥ 19 | Cài qua Palette Manager |

---

## 2. Khởi động Node-RED

```bash
node-red
```

Truy cập giao diện tại: **http://localhost:1880**

---

## 3. Cài đặt Palettes (Node packages)

Vào **Menu (☰) → Manage Palette → Install** và tìm cài:

| Package | Chức năng |
|---|---|
| `node-red-dashboard` | Gauge, Chart, Button, Text UI |
| `node-red-contrib-telegrambot` | Gửi cảnh báo qua Telegram Bot |

Sau khi cài xong, **restart Node-RED**.

---

## 4. Import Flow

1. Vào **Menu (☰) → Import**
2. Chọn **"select a file to import"**
3. Chọn file `flows.json` trong thư mục này
4. Nhấn **Import**
5. Nhấn **Deploy** (nút đỏ góc trên phải)

---

## 5. Cấu hình MQTT Broker

Sau khi import, double-click vào bất kỳ node **mqtt in** nào:

1. Click biểu tượng ✏️ bên cạnh Server
2. Điền:
   - **Server**: IP laptop chạy Mosquitto (ví dụ `192.168.1.100`)
   - **Port**: `1883`
3. Nhấn **Update → Done → Deploy**

---

## 6. Cấu hình Telegram Bot

### 6a. Tạo Bot

1. Nhắn tin **@BotFather** trên Telegram: `/newbot`
2. Đặt tên và username cho bot
3. Lưu **Bot Token** (dạng `123456:ABC-DEF...`)

### 6b. Lấy Chat ID

1. Nhắn tin với bot của bạn, sau đó truy cập:
   ```
   https://api.telegram.org/bot<TOKEN>/getUpdates
   ```
2. Tìm trường `chat.id` trong JSON trả về

### 6c. Cấu hình trong Node-RED

**Cách 1 — Environment Variables (khuyến nghị):**

Khởi động Node-RED với biến môi trường:

```bash
TELEGRAM_CHAT_ID=123456789 THINGSPEAK_WRITE_API_KEY=XXXXXXXX node-red
```

Trên Windows (PowerShell):
```powershell
$env:TELEGRAM_CHAT_ID = "123456789"
$env:THINGSPEAK_WRITE_API_KEY = "XXXXXXXX"
node-red
```

**Cách 2 — File `config/secrets.env`** (khuyến nghị): `start_all.ps1` tự nạp file này trước khi chạy Node-RED.

**Cấu hình Telegram Bot Config node:**

1. Double-click node **"Gửi Telegram"**
2. Click ✏️ bên cạnh Bot
3. Điền **Bot Token** vào trường Token
4. Nhấn **Update → Done → Deploy**

---

## 7. Cấu hình ThingSpeak

1. Đăng ký tài khoản tại https://thingspeak.com
2. Tạo Channel mới với 6 fields:
   - Field 1: `pipe_level_cm`
   - Field 2: `fill_pct`
   - Field 3: `rise_rate_cm_min`
   - Field 4: `road_depth_cm`
   - Field 5: `cong1_alert` (0=green, 1=yellow, 2=red)
   - Field 6: `duong1_alert` (0=green, 1=yellow, 2=red)
3. Lấy **Write API Key** từ tab "API Keys"
4. Truyền qua environment variable `THINGSPEAK_WRITE_API_KEY`

---

## 8. Truy cập Dashboard

Sau khi deploy thành công:

**URL Dashboard**: http://localhost:1880/ui

**Tab "Flood Monitor"**
- Cống 1: độ đầy L, mức cảnh báo, mực nước, tốc độ dâng (cửa sổ 1 phút), kết nối
- Đường 1: độ sâu ngập, mức đèn tại chỗ, trạng thái còi, nút 🔇 Tắt còi / 🔔 Bật lại còi
- Cảnh báo & yêu cầu vận hành: tình trạng chung, yêu cầu bật bơm/mở van đang chờ, nút ✅ Xác nhận, nhật ký sự kiện
- Dự báo AI, biểu đồ lịch sử

**Tab "Quản lý thiết bị"**
- Bảng thiết bị (online/offline, mức, số đo, RSSI, uptime, lỗi đo, firmware) – node mới tự xuất hiện
- Form cấu hình từ xa cho cong1 / duong1 (gửi retained `flood/<id>/config`, node xác nhận qua `flood/<id>/event`)
- Nút hiệu chuẩn: đo lại H (cống rỗng) / d0 (đường khô)

---

## 9. Troubleshooting

| Vấn đề | Giải pháp |
|---|---|
| Node MQTT màu đỏ "disconnected" | Kiểm tra Mosquitto đang chạy, kiểm tra IP/port |
| Dashboard không hiện | Đảm bảo cài `node-red-dashboard`, restart Node-RED |
| Telegram không nhận tin | Kiểm tra Bot Token và Chat ID; kiểm tra internet |
| ThingSpeak báo lỗi 400 | Kiểm tra Write API Key; tối thiểu 15s giữa các lần ghi |
| Không nhận dữ liệu từ ESP32 | Dùng MQTT Explorer để kiểm tra topic đang có message không |

---

## 10. Cấu trúc Flow

```
flood/+/level  → Định tuyến (có L,m = cống | có h = đường)
                   ├→ Node cống: TB trượt 5 mẫu, v theo 1 phút, hysteresis + giữ 30 s ─┐
                   └→ Node đường: lấy mức do ESP32 tự phân ───────────────────────────┤
flood/+/status (Last Will) ─────────────────────────────────────────────────────────────┤
Inject 30 s (nhắc lại 10 phút, watchdog 30 s) ──────────────────────────────────────────┤
Telegram callback "✅ Đã xử lý" / nút Xác nhận dashboard / lệnh /status ────────────────┤
                                                                                        ▼
                                                     Bộ quyết định cảnh báo & sự cố → Telegram, Dashboard, Nhật ký
flood/+/info   → bảng thiết bị, điền form cấu hình
flood/+/event  → nhật ký; "calibrated" → cập nhật lại config retained
Form cấu hình  → kiểm tra → flood/<id>/config (retained)
Nút còi / hiệu chuẩn → flood/duong1/alarm/cmd, flood/<id>/cmd
Inject 15 s    → gộp 2 node → ThingSpeak (1 request)
```
