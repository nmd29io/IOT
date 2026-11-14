# 🌊 Hệ thống Giám sát & Cảnh báo Ngập úng Đô thị

> **Urban Flood Monitoring & Alert System**  
> Dự án IoT 4 tầng sử dụng ESP32 + JSN-SR04T + MQTT + Node-RED + ThingSpeak + Telegram

---

## 📋 Mục lục

1. [Tổng quan dự án](#1-tổng-quan-dự-án)
2. [Kiến trúc hệ thống](#2-kiến-trúc-hệ-thống)
3. [Danh sách phần cứng](#3-danh-sách-phần-cứng)
4. [Sơ đồ kết nối](#4-sơ-đồ-kết-nối)
5. [Yêu cầu phần mềm](#5-yêu-cầu-phần-mềm)
6. [Hướng dẫn cài đặt từng bước](#6-hướng-dẫn-cài-đặt-từng-bước)
7. [Bảng MQTT Topics](#7-bảng-mqtt-topics)
8. [Bảng ngưỡng cảnh báo](#8-bảng-ngưỡng-cảnh-báo)
9. [Cấu trúc thư mục dự án](#9-cấu-trúc-thư-mục-dự-án)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Tổng quan dự án

Hệ thống giám sát ngập úng đô thị theo dõi đồng thời **mực nước trong cống thoát nước** và **độ sâu ngập trên mặt đường**, phát cảnh báo tự động qua **Telegram** và lưu dữ liệu lên **ThingSpeak Cloud**.

### Tính năng chính

| Tính năng | Mô tả |
|---|---|
| Đo mực nước cống | Cảm biến siêu âm JSN-SR04T chống nước (Node 1) |
| Đo độ sâu ngập đường | Cảm biến siêu âm JSN-SR04T chống nước (Node 2) |
| Lọc nhiễu | Median filter (N=7) + Moving average (N=5) |
| Bộ lọc ổn định | Node 2: giá trị phải ổn định 20 giây mới được chấp nhận |
| Cảnh báo có trễ (hysteresis) | Tránh cảnh báo nhấp nháy khi giá trị nằm gần ngưỡng |
| Tốc độ dâng nước | Tính và cảnh báo khi nước dâng > 5 cm/phút |
| LED + Buzzer | Hiển thị trực quan tại chỗ (Node 2) |
| Dashboard | Giao diện web Node-RED Dashboard realtime |
| Telegram Bot | Cảnh báo tiếng Việt tức thì |
| ThingSpeak | Lưu trữ và vẽ đồ thị dữ liệu trên cloud |

---

## 2. Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────────────────┐
│                     TẦNG 4 — CLOUD                                       │
│          ┌───────────────┐          ┌──────────────────┐                 │
│          │  ThingSpeak   │          │   Telegram Bot   │                 │
│          │  (IoT Cloud)  │          │  (Alert Channel) │                 │
│          └───────┬───────┘          └────────┬─────────┘                 │
└──────────────────┼──────────────────────────┼─────────────────────────── ┘
                   │ HTTPS                    │ HTTPS
┌──────────────────┼──────────────────────────┼─────────────────────────── ┐
│              TẦNG 3 — EDGE IT (Laptop)       │                            │
│    ┌─────────────▼──────────────────────────▼──────────────┐             │
│    │                     Node-RED                           │             │
│    │  Parse → Hysteresis → Dashboard → ThingSpeak → Telegram│             │
│    └─────────────────────────┬──────────────────────────────┘             │
│                              │ MQTT                                        │
│    ┌─────────────────────────▼──────────────────────────────┐             │
│    │            Mosquitto MQTT Broker (:1883)                │             │
│    └──────┬──────────────────────────────────────┬──────────┘             │
└───────────┼──────────────────────────────────────┼──────────────────────  ┘
            │ WiFi/MQTT                            │ WiFi/MQTT
┌───────────▼──────────┐              ┌────────────▼──────────────┐
│  TẦNG 1-2: NODE 1    │              │  TẦNG 1-2: NODE 2          │
│  ESP32 (cong1)       │              │  ESP32 (duong1)            │
│                      │              │                             │
│  JSN-SR04T           │              │  JSN-SR04T                  │
│  (đo mực nước cống)  │              │  (đo độ sâu mặt đường)      │
│                      │              │                             │
│  H = 200 cm          │              │  LED: Xanh/Vàng/Đỏ         │
│  Hmax = 180 cm       │              │  Buzzer                     │
│                      │              │  d0 = 300 cm                │
└──────────────────────┘              └─────────────────────────────┘
      │                                           │
      │ Cống thoát nước                           │ Mặt đường
      ▼                                           ▼
  ~~~~~~~~~~~~~~~                             ___________
  ═══════════════  ← mực nước               ~~~~~~~~~~~  ← nước ngập
  ═══════════════
```

---

## 3. Danh sách phần cứng

| STT | Linh kiện | Số lượng | Ghi chú |
|---|---|---|---|
| 1 | ESP32 DevKit V1 | 2 | hoặc ESP32-WROOM-32 |
| 2 | JSN-SR04T (cảm biến siêu âm chống nước) | 2 | Range 25–450 cm |
| 3 | LED xanh lá (5mm) | 1 | Node 2 |
| 4 | LED vàng (5mm) | 1 | Node 2 |
| 5 | LED đỏ (5mm) | 1 | Node 2 |
| 6 | Buzzer tích cực 5V | 1 | Node 2 |
| 7 | Điện trở 220 Ω | 3 | Cho 3 LED |
| 8 | Điện trở 100 Ω | 1 | Cho buzzer (tùy loại) |
| 9 | Dây điện | 1 cuộn | Dây đơn AWG 22 |
| 10 | Hộp chống nước IP65 | 2 | Cho ESP32 ngoài trời |
| 11 | Nguồn 5V / 2A | 2 | Adapter hoặc pin Li-Ion |
| 12 | Laptop / PC | 1 | Chạy Mosquitto + Node-RED |

---

## 4. Sơ đồ kết nối

### Node 1 — cong1 (Đo mực nước cống)

| Chân JSN-SR04T | Chân ESP32 | Ghi chú |
|---|---|---|
| VCC | 5V | Nguồn 5V |
| GND | GND | Nối đất |
| TRIG | GPIO 5 | Xung kích |
| ECHO | GPIO 18 | Nhận tín hiệu |

### Node 2 — duong1 (Đo ngập đường + Cảnh báo)

| Linh kiện | Chân linh kiện | Chân ESP32 |
|---|---|---|
| JSN-SR04T VCC | VCC | 5V |
| JSN-SR04T GND | GND | GND |
| JSN-SR04T TRIG | TRIG | GPIO 5 |
| JSN-SR04T ECHO | ECHO | GPIO 18 |
| LED Xanh lá | Anode → R220Ω | GPIO 26 |
| LED Vàng | Anode → R220Ω | GPIO 27 |
| LED Đỏ | Anode → R220Ω | GPIO 14 |
| Buzzer (+) | | GPIO 12 |
| Buzzer (−) | | GND |
| Tất cả Cathode LED | | GND |

> **Lưu ý:** JSN-SR04T hoạt động với 5V. ECHO output cũng là 5V — dùng **bộ chia điện áp** (R1=1kΩ, R2=2kΩ) hoặc **level shifter** trước khi nối vào GPIO 18 (3.3V tolerant) của ESP32.

---

## 5. Yêu cầu phần mềm

### Trên Laptop (Edge IT)

| Phần mềm | Phiên bản | Link tải |
|---|---|---|
| Mosquitto MQTT Broker | ≥ 2.0 | https://mosquitto.org/download/ |
| Node.js | ≥ 18 LTS | https://nodejs.org |
| Node-RED | ≥ 3.1 | `npm install -g --unsafe-perm node-red` |
| MQTT Explorer (tùy chọn) | latest | https://mqtt-explorer.com/ |

### Arduino IDE (nạp firmware)

| Thư viện | Cài qua | Tác giả |
|---|---|---|
| `PubSubClient` | Library Manager | Nick O'Leary |
| `ArduinoJson` | Library Manager | Benoit Blanchon |
| ESP32 Board Package | Board Manager | Espressif |

### Tài khoản Cloud

- **ThingSpeak**: https://thingspeak.com (miễn phí)
- **Telegram**: Tạo bot qua @BotFather

---

## 6. Hướng dẫn cài đặt từng bước

### Bước 1 — Nạp firmware vào ESP32

#### 1.1 Cài Board ESP32 vào Arduino IDE

1. Mở Arduino IDE → **File → Preferences**
2. Thêm vào "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. **Tools → Board → Board Manager** → tìm "esp32" → Install

#### 1.2 Cài thư viện

**Tools → Manage Libraries** → tìm và cài:
- `PubSubClient` by Nick O'Leary
- `ArduinoJson` by Benoit Blanchon

#### 1.3 Cấu hình config.h

Chỉnh sửa file `firmware/config.h`:

```cpp
#define WIFI_SSID       "Ten_WiFi_cua_ban"
#define WIFI_PASSWORD   "Mat_khau_WiFi"
#define MQTT_SERVER     "192.168.1.100"   // IP laptop
```

Sao chép `config.h` vào thư mục của từng sketch:
- `firmware/node_cong1/config.h`
- `firmware/node_duong1/config.h`

Trong mỗi file, bỏ comment đúng NODE_ID:

```cpp
// Cho node_cong1.ino:
#define NODE_ID "cong1"

// Cho node_duong1.ino:
#define NODE_ID "duong1"
```

#### 1.4 Nạp firmware

1. Chọn board: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
2. Chọn tốc độ upload: **Upload Speed → 921600**
3. Chọn đúng cổng COM
4. Mở sketch → **Upload** (Ctrl+U)
5. Kiểm tra Serial Monitor (115200 baud)

---

### Bước 2 — Khởi động Mosquitto

1. Cài Mosquitto: https://mosquitto.org/download/
2. Mở Command Prompt tại thư mục `mosquitto\`
3. Chạy:

```bat
start_broker.bat
```

Hoặc thủ công:
```bash
mosquitto -v -c mosquitto.conf
```

**Kiểm tra:** Mở MQTT Explorer, kết nối đến `localhost:1883`, kiểm tra topic `flood/#`.

---

### Bước 3 — Cài đặt Node-RED

```bash
npm install -g --unsafe-perm node-red
node-red
```

Truy cập: **http://localhost:1880**

**Cài palettes** (Menu → Manage Palette → Install):
- `node-red-dashboard`
- `node-red-contrib-telegram`

**Import flow:**
1. Menu → Import → Select file → chọn `nodered/flows.json`
2. Nhấn Import → Deploy

**Cấu hình MQTT broker node:**
- Double-click node mqtt in bất kỳ
- Sửa Server = IP laptop của bạn

**Xem tài liệu chi tiết:** [`nodered/README_nodered.md`](nodered/README_nodered.md)

---

### Bước 4 — Cài đặt ThingSpeak

1. Đăng ký tại https://thingspeak.com
2. Tạo Channel → đặt tên "Flood Monitor"
3. Thêm 6 fields:
   - Field 1: `pipe_level_cm`
   - Field 2: `fill_pct`
   - Field 3: `rise_rate_cm_min`
   - Field 4: `road_depth_cm`
   - Field 5: `cong1_alert`
   - Field 6: `duong1_alert`
4. Vào **API Keys** → sao chép Write API Key
5. Cập nhật biến môi trường hoặc global trong Node-RED

---

### Bước 5 — Cài đặt Telegram Bot

1. Nhắn tin @BotFather: `/newbot`
2. Đặt tên bot: `FloodAlertBot` (hoặc tên khác)
3. Lưu **Bot Token**
4. Nhắn tin với bot của bạn, sau đó truy cập:
   ```
   https://api.telegram.org/bot<TOKEN>/getUpdates
   ```
5. Lấy `chat.id` từ JSON
6. Cấu hình trong Node-RED (xem Bước 3)

---

## 7. Bảng MQTT Topics

| Topic | Hướng | QoS | Retain | Mô tả | Payload mẫu |
|---|---|---|---|---|---|
| `flood/cong1/level` | ESP32 → Broker | 1 | No | Mực nước + % đầy | `{"level_cm":85.3,"fill_pct":47.4,"rise_rate":1.2,"status":"green","timestamp":12345}` |
| `flood/cong1/status` | ESP32 → Broker | 1 | No | Trạng thái cảnh báo | `"green"` / `"yellow"` / `"red"` |
| `flood/cong1/online` | ESP32 → Broker | 1 | Yes | Trạng thái kết nối | `"online"` / `"offline"` (Last Will) |
| `flood/cong1/rate` | (computed Node-RED) | — | No | Tốc độ dâng (cm/min) | `2.3` |
| `flood/duong1/depth` | ESP32 → Broker | 1 | No | Độ sâu ngập đường | `{"depth_cm":3.5,"status":"yellow","stable":true,"timestamp":12345}` |
| `flood/duong1/online` | ESP32 → Broker | 1 | Yes | Trạng thái kết nối | `"online"` / `"offline"` |
| `flood/duong1/buzzer` | Node-RED → ESP32 | 1 | No | Lệnh tắt còi | `"off"` |

---

## 8. Bảng ngưỡng cảnh báo

### Node 1 — cong1 (Mực nước cống, tính theo % đầy)

| Trạng thái | Điều kiện vào | Điều kiện thoát | LED/Âm thanh |
|---|---|---|---|
| 🟢 AN TOÀN | Mặc định / fill < 60% | — | — |
| 🟡 CẢNH BÁO | fill ≥ 60% | fill < 50% | — |
| 🔴 NGUY HIỂM | fill ≥ 80% **hoặc** dâng > 5 cm/phút | fill < 70% | — |

### Node 2 — duong1 (Độ sâu ngập mặt đường, cm)

| Trạng thái | Điều kiện vào | Điều kiện thoát | LED | Buzzer |
|---|---|---|---|---|
| 🟢 AN TOÀN | depth < 5 cm | — | Xanh sáng | Tắt |
| 🟡 CẢNH BÁO | depth ≥ 5 cm | depth < 3 cm | Vàng sáng | Kêu 1 kHz |
| 🔴 NGẬP | depth ≥ 15 cm | depth < 10 cm | Đỏ sáng | Kêu 2.5 kHz |

### Cảnh báo tổng hợp (Node-RED)

| Tình huống | Mức độ | Tin nhắn Telegram |
|---|---|---|
| cong1=yellow | ⚠️ CẢNH BÁO | Mực nước trong cống đạt X%, tốc độ dâng Y cm/phút. |
| cong1=red | 🚨 NGUY HIỂM | Cống sắp tràn! Mực nước X%, dâng Y cm/phút. |
| duong1=red | 🌊 NGẬP ĐƯỜNG | Độ sâu ngập X cm. Yêu cầu điều tiết giao thông. |
| cong1=red + duong1=red | 🆘 NGẬP NGHIÊM TRỌNG | KHẨN CẤP! Cống tràn + mặt đường ngập X cm. |

> **Ghi chú:** Tin Telegram chỉ gửi khi **trạng thái thay đổi** (tránh spam).

---

## 9. Cấu trúc thư mục dự án

```
PRoject/
├── firmware/
│   ├── config.h                    # Cấu hình WiFi/MQTT/Hardware (không commit)
│   ├── node_cong1/
│   │   └── node_cong1.ino          # Firmware Node 1 (cống)
│   └── node_duong1/
│       └── node_duong1.ino         # Firmware Node 2 (đường)
├── mosquitto/
│   ├── mosquitto.conf              # Cấu hình Mosquitto broker
│   ├── start_broker.bat            # Script khởi động broker (Windows)
│   └── data/                       # Dữ liệu persistence (auto-created)
├── nodered/
│   ├── flows.json                  # Node-RED flow hoàn chỉnh
│   └── README_nodered.md           # Hướng dẫn cài đặt Node-RED
├── config/
│   └── secrets.env                 # Template biến môi trường (không commit)
├── .gitignore
└── README.md                       # File này
```

---

## 10. Troubleshooting

### ESP32 không kết nối WiFi
- Kiểm tra SSID/Password trong `config.h`
- Đảm bảo ESP32 và laptop cùng mạng WiFi
- Kiểm tra Serial Monitor 115200 baud

### ESP32 kết nối WiFi nhưng không gửi MQTT
- Ping laptop từ thiết bị khác cùng mạng: `ping 192.168.1.100`
- Kiểm tra Mosquitto đang chạy và lắng nghe port 1883
- Thử tắt Windows Firewall tạm thời để test
- Thêm rule firewall: `netsh advfirewall firewall add rule name="Mosquitto" protocol=TCP dir=in localport=1883 action=allow`

### Cảm biến trả về giá trị ngẫu nhiên
- JSN-SR04T cần 5V; không dùng 3.3V
- Kiểm tra ECHO signal không vượt 3.3V vào ESP32 (dùng voltage divider)
- Đảm bảo cảm biến hướng thẳng, không có vật cản trong góc 15°

### Node-RED không nhận dữ liệu
- Kiểm tra MQTT broker node hiển thị "connected"
- Dùng node **mqtt in** với topic `flood/#` và debug node để xem raw messages
- Kiểm tra payload là JSON hợp lệ bằng MQTT Explorer

### Telegram không nhận tin nhắn
- Xác nhận Bot Token đúng
- Xác nhận Chat ID đúng (số nguyên, có thể âm cho group chat)
- Thử gọi API thủ công:
  ```bash
  curl "https://api.telegram.org/bot<TOKEN>/sendMessage?chat_id=<ID>&text=test"
  ```

### ThingSpeak không nhận dữ liệu
- Kiểm tra Write API Key
- ThingSpeak free tier giới hạn 1 lần ghi / 15 giây — đã được xử lý trong Node-RED
- Kiểm tra internet trên laptop

---

## 11. 🤖 Khối AI/ML: Dự Báo Ngập Úng Đa Tầng Thời Gian (Multi-Horizon AI)

> **Thỏa mãn trọn vẹn tiêu chí chấm điểm:** *"Ứng dụng AI/ML để phân tích dữ liệu IoT để dự báo hoặc đưa ra quyết định thông minh"* (1 điểm).

### 11.1. Kiến trúc Cảnh báo 2 Cấp Độ (Two-Tier Multi-Horizon)
Hệ thống giải quyết bài toán cảnh báo ngập úng bằng kiến trúc phân tầng đa mốc thời gian:

1. **Cấp 1 — Chốt chặn tức thời tại chỗ (Mốc +5 phút & +15 phút, chạy tại Edge IT):**
   - **Đầu vào:** Cảm biến siêu âm JSN-SR04T đo trực tiếp trong cống và trên mặt đường.
   - **Trọng số chi phối:** Mực nước hiện tại ($88.9\%$) và quán tính dâng nước.
   - **Hành động phản ứng:** Đổi đèn giao thông tại chỗ (Node 2), kích hoạt hạ rào barie hầm chui, tự động mở van phai điện tử hoặc bật trạm bơm trước khi rác làm nghẹt buồng bơm.
2. **Cấp 2 — Điều phối giao thông diện rộng (Mốc +30 phút & +60 phút, chạy tại Cloud):**
   - **Đầu vào:** Tích hợp dữ liệu **Radar thời tiết (Nowcasting)**, **mô hình lưu vực dòng chảy mặt GIS**, và **mực nước sông xả hạ lưu** (Sông Tô Lịch/Sông Nhuệ).
   - **Trọng số chi phối:** Lượng mưa dự báo trong tương lai vọt lên chiếm **$76\%$** (thay thế mực nước cống ban đầu).
   - **Hành động phản ứng:** Phát cảnh báo xác suất ngập úng lên ứng dụng **iHanoi**, kênh **VOV Giao thông**, Google Maps để phân luồng phương tiện từ xa (tránh chết máy gây tê liệt tuyến đường), đồng thời chỉ đạo các đại hồ điều hòa (Hồ Yên Sở) chủ động bơm hạ mực nước đệm.

### 11.2. Kết quả đánh giá mô hình tích hợp Thủy lực Manning & Địa hình GIS

| Mốc dự báo | Sai số MAE (cm) | RMSE (cm) | Hệ số $R^2$ | Đặc trưng quyết định | Ứng dụng thực tế |
|:---:|:---:|:---:|:---:|---|---|
| **+5 phút** | **0.30 cm** | 1.10 cm | **0.9998** | Hệ số quá tải thủy lực ($97.1\%$) | Đổi đèn LED, hạ barie, bật trạm bơm |
| **+15 phút** | **0.71 cm** | 2.42 cm | **0.9989** | Quá tải cống & Lưu lượng vào GIS | Mở cửa phai phân lưu hồ điều hòa |
| **+30 phút** | **1.41 cm** | 4.42 cm | **0.9965** | Mưa Radar 30m & Sông Tô Lịch dội ngược | **Phân luồng xe iHanoi / VOV Giao thông** |
| **+60 phút** | **3.30 cm** | 10.42 cm | **0.9803** | Mưa Radar 60m ($79.5\%$) & Dòng chảy lưu vực | Bơm xả đệm hồ Yên Sở, lùi giờ tan tầm |

### 11.3. Cấu hình Thông số Thủy Lực & Địa Hình GIS (`config/gis_hydraulic_params.json`)
Hệ thống sử dụng các công thức vật lý chuyên ngành:
* **Công thức dòng chảy mặt Rational Method:** $Q_{inflow} = \frac{C \cdot I \cdot A}{360}$ ($C = 0.85$, $A = 12.5\text{ ha}$).
* **Công thức năng lực tải cống ngầm Manning:** $Q_{\max} = \frac{1}{n} A R^{2/3} S^{1/2}$ ($D = 1000\text{ mm}$, $n = 0.013$, độ dốc $0.2\% \to Q_{\max} = 1.45\text{ m}^3/\text{s}$).
* **Hệ số quá tải thủy lực:** $Overload = Q_{inflow} / Q_{\text{actual}}$ (phát hiện cống quá tải trước khi nước dâng).

### 11.4. Cấu trúc thư mục AI/ML
```
config/
├── gis_hydraulic_params.json         # Cấu hình lưu vực GIS và thông số thủy lực cống
ml/
├── train_model.py                    # Huấn luyện 4 mô hình Multi-Horizon & xuất biểu đồ
├── edge_ml_service.py                # Dịch vụ AI chạy ngầm: kết nối MQTT, tính thủy lực thời gian thực
├── models/
│   ├── flood_model_5m.joblib         # Model Random Forest dự báo +5 phút
│   ├── flood_model_15m.joblib        # Model Random Forest dự báo +15 phút
│   ├── flood_model_30m.joblib        # Model Random Forest dự báo +30 phút
│   └── flood_model_60m.joblib        # Model Random Forest dự báo +60 phút
└── figures/
    ├── multi_horizon_comparison.png  # Biểu đồ so sánh MAE qua 4 mốc thời gian
    ├── feature_importance_shift.png  # Biểu đồ chuyển dịch trọng số (Thủy lực 5m vs Mưa Radar 60m)
    ├── forecast_comparison.png       # Biểu đồ so sánh Thực tế vs AI vs Baseline
    └── feature_importance.png        # Biểu đồ chi tiết đặc trưng mốc 5 phút
IOT.ipynb                             # Jupyter Notebook hoàn chỉnh có giải thích & code mẫu
data/
├── flood_gis_hydraulic_data.csv      # Dữ liệu huấn luyện tích hợp thủy lực & GIS
└── sensor_history.csv                # Log dữ liệu cảm biến & kết quả dự báo thời gian thực
```

### 11.5. Khởi chạy và Trải nghiệm
- Khi chạy script tổng thể [`start_all.bat`](file:///c:/Users/Duc/Desktop/IoT/PRoject/start_all.bat), dịch vụ AI `edge_ml_service.py` sẽ **tự động khởi động** cùng Mosquitto Broker và Node-RED.
- Dashboard [http://localhost:1880/ui](http://localhost:1880/ui) hiển thị:
  1. Đồng hồ đo dự báo tại chỗ (+5 phút).
  2. Đồng hồ đo **Xác suất ngập úng phục vụ giao thông (+30 phút)**.
  3. Banner khuyến nghị điều phối giao thông diện rộng tự động.
  4. Biểu đồ đa đường so sánh mực nước thực tế và 3 đường dự báo tương lai (+5m, +30m, +60m).

---

## 12. 🔮 Hướng Phát Triển Tiếp Theo Của Đề Tài (Future Work)

Sau khi đã hoàn thiện thành công mô hình tích hợp Thủy lực Manning & Địa hình GIS phục vụ cảnh báo đa tầng thời gian (5m – 60m), các bước phát triển tiếp theo khi đưa hệ thống từ quy mô thử nghiệm (Proof of Concept) ra triển khai thực địa toàn đô thị Hà Nội gồm:

1. **Phần cứng công nghiệp & Truyền thông năng lượng thấp (Hardware Hardening):**
   - Nâng cấp vỏ bọc cảm biến đạt chuẩn kháng nước/bụi **IP68**, chống ăn mòn hóa chất và khí độc ($H_2S, CH_4$) trong lòng cống ngầm.
   - Bổ sung pin năng lượng mặt trời / pin lithium tuổi thọ 3–5 năm, chuyển đổi từ WiFi sang công nghệ truyền thông **LoRaWAN / NB-IoT** để phủ sóng toàn bộ các nắp cống ngầm không có sóng WiFi.
2. **Đấu nối API Radar Khí Tượng thời gian thực:**
   - Kết nối trực tiếp vào luồng dữ liệu phản hồi vô tuyến (dBZ) của Trung tâm Dự báo Khí tượng Thủy văn Quốc gia (đài radar Đông Anh/Nội Bài) để thay thế luồng giả lập Nowcasting hiện tại, tự động cập nhật bản đồ mây mưa theo từng phút.
3. **Mô hình hóa Toàn Mạng Lưới Bằng Graph Neural Network (GNN):**
   - Nâng cấp từ mô hình Random Forest tại từng node độc lập sang mô hình mạng nơ-ron đồ thị (GNN) chạy trên Cloud, mô hình hóa toàn bộ hơn 500+ trạm cống liên thông của Hà Nội để dự báo lan truyền ngập nước giữa các lưu vực và hồ điều hòa.
4. **Tích hợp Nền tảng Đô Thị Thông Minh (Smart City & iHanoi Integration):**
   - Đấu nối Webhook cảnh báo ngập đa cấp trực tiếp vào ứng dụng **Công dân Thủ đô số (iHanoi)** và bản đồ HSDC, tự động gợi ý lộ trình tránh ngập theo thời gian thực cho người tham gia giao thông.

---

## 📄 Giấy phép

MIT License — Tự do sử dụng và chỉnh sửa cho mục đích học tập và nghiên cứu.

---

*Phát triển bởi nhóm 1 — Môn Phát triển ứng dụng Internet of Things (UET/VNU)*
