/**
 * config.example.h — Mẫu cấu hình cho firmware flood_node
 *
 * CÁCH DÙNG:
 *   1. Sao chép file này thành  config.h  (cùng thư mục firmware/flood_node/).
 *   2. Chọn NODE_MODE, sửa WiFi, IP broker rồi nạp.
 *   config.h đã nằm trong .gitignore — KHÔNG commit mật khẩu WiFi lên repo.
 *
 * Các thông số lắp đặt (H, Hmax, d0, ngưỡng, chu kỳ đo) bên dưới chỉ là
 * GIÁ TRỊ MẶC ĐỊNH lần đầu. Sau đó đổi TỪ XA trên dashboard / web
 * (topic flood/<id>/config) hoặc bấm "Hiệu chuẩn" — node lưu vào flash (NVS).
 */
#pragma once

// ─── Chế độ ──────────────────────────────────────────────────────────────────
//  MODE_DUAL : 1 ESP32 gắn 2 cảm biến, chạy 2 node logic (cống + đường).
//              Mỗi node có kết nối MQTT, Last Will, cấu hình riêng → phía
//              Node-RED / cloud thấy 2 node như khi dùng 2 mạch.
//  MODE_DRAIN: mạch chỉ làm node cống (cảm biến 1).
//  MODE_ROAD : mạch chỉ làm node đường (cảm biến 1 + đèn + còi).
#define NODE_MODE   MODE_DUAL

#define DRAIN_ID    "cong1"         // mã node cống
#define ROAD_ID     "duong1"        // mã node đường

// ─── WiFi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID       "UET-Wifi-T4 5Ghz"
#define WIFI_PASSWORD   ""

// ─── MQTT broker (laptop chạy Mosquitto) ─────────────────────────────────────
#define MQTT_SERVER     "192.168.1.100"
#define MQTT_PORT       1883
#define MQTT_USER       ""          // để trống nếu broker cho phép anonymous
#define MQTT_PASSWORD   ""

// ─── Chân cảm biến JSN-SR04T ─────────────────────────────────────────────────
// ECHO phải qua cầu chia áp 5V → 3,3V trước khi vào ESP32.
// Khoảng cách nhỏ nhất được chấp nhận (cm). JSN-SR04T có vùng mù ~20 cm: đặt
// thấp hơn thì đo gần được nhưng có thể nhiễu. HC-SR04 / AJ-SR04M đo được từ ~3 cm.
#define MIN_RANGE_CM    20.0f
// Cảm biến 1: node cống (MODE_DUAL / MODE_DRAIN) hoặc node đường (MODE_ROAD)
#define SENSOR1_TRIG    5
#define SENSOR1_ECHO    18
// Cảm biến 2: node đường trong MODE_DUAL
#define SENSOR2_TRIG    19
#define SENSOR2_ECHO    21

// ─── Đèn / còi của node đường ────────────────────────────────────────────────
#define LED_GREEN_PIN   26
#define LED_YELLOW_PIN  27
#define LED_RED_PIN     14
// Không dùng GPIO12 cho còi: chân strapping, bị kéo lên lúc khởi động
// sẽ làm ESP32 không boot được.
#define BUZZER_PIN      25
#define BUZZER_PASSIVE  0           // 1 = còi thụ động (cần phát tần số), 0 = còi chủ động

// ─── Giá trị mặc định (đổi được từ xa) ──────────────────────────────────────
// Node cống:  m = H - d ;  L(%) = m / Hmax * 100
#define DEFAULT_H_CM        60.0f   // khoảng cách cảm biến → đáy ống (cm)
#define DEFAULT_HMAX_CM     35.0f   // chiều sâu dùng để tính % đầy (cm)
// Node đường: h = d0 - d
#define DEFAULT_D0_CM       50.0f   // cảm biến → mặt đường khô (cm). Phải ≥ MIN_RANGE_CM + độ ngập lớn nhất cần đo

// Ngưỡng node đường (cm) — giá trị theo báo cáo; khi demo trên khay nông
// nên quy đổi nhỏ lại (ví dụ 2 / 4 / 3 / 1.5) bằng form cấu hình trên dashboard.
#define DEFAULT_T_YELLOW      10.0f // h ≥ t_yellow           → VÀNG
#define DEFAULT_T_RED         20.0f // h > t_red              → ĐỎ
#define DEFAULT_T_RED_LOW     15.0f // đang ĐỎ, chỉ về VÀNG khi h < t_red_low
#define DEFAULT_T_YELLOW_LOW   8.0f // đang VÀNG, chỉ về XANH khi h < t_yellow_low
#define DEFAULT_STABLE_S        20  // mức mới phải giữ ổn định ≥ N giây mới được công nhận

#define DEFAULT_PERIOD_MS     2000  // chu kỳ đo (ms)
