/**
 * config.example.h — Mẫu cấu hình cho firmware flood_node
 *
 * CÁCH DÙNG:
 *   1. Sao chép file này thành  config.h  (cùng thư mục firmware/flood_node/).
 *   2. Sửa NODE_ID, NODE_ROLE, WiFi, IP broker cho từng mạch trước khi nạp.
 *   config.h đã nằm trong .gitignore — KHÔNG commit mật khẩu WiFi lên repo.
 *
 * Các thông số lắp đặt (H, Hmax, d0, ngưỡng, chu kỳ đo) bên dưới chỉ là
 * GIÁ TRỊ MẶC ĐỊNH lần đầu. Sau đó có thể đổi TỪ XA trên dashboard
 * (topic flood/<id>/config) hoặc bấm "Hiệu chuẩn" — node lưu vào bộ nhớ
 * flash (NVS) nên khởi động lại vẫn giữ giá trị mới.
 */
#pragma once

// ─── Định danh node ──────────────────────────────────────────────────────────
// Thêm node mới: chỉ cần đổi NODE_ID (ví dụ "cong2", "duong2") và NODE_ROLE.
#define NODE_ID     "duong1"        // "cong1" | "duong1" | ...
#define NODE_ROLE   ROLE_ROAD       // ROLE_DRAIN (node trong cống) | ROLE_ROAD (node trên đường)

// ─── WiFi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID       "Ten_WiFi"
#define WIFI_PASSWORD   "Mat_khau_WiFi"

// ─── MQTT broker (laptop chạy Mosquitto) ─────────────────────────────────────
#define MQTT_SERVER     "192.168.1.100"
#define MQTT_PORT       1883
#define MQTT_USER       ""          // để trống nếu broker cho phép anonymous
#define MQTT_PASSWORD   ""

// ─── Chân cảm biến JSN-SR04T ─────────────────────────────────────────────────
// ECHO phải qua cầu chia áp 5V → 3,3V trước khi vào ESP32.
#define TRIG_PIN        5
#define ECHO_PIN        18

// ─── Chân đèn / còi (chỉ dùng ở node đường) ─────────────────────────────────
#define LED_GREEN_PIN   26
#define LED_YELLOW_PIN  27
#define LED_RED_PIN     14
// Không dùng GPIO12 cho còi: đó là chân strapping, bị kéo lên lúc khởi động
// sẽ làm ESP32 không boot được.
#define BUZZER_PIN      25
#define BUZZER_PASSIVE  0           // 1 = còi thụ động (cần phát tần số), 0 = còi chủ động

// ─── Giá trị mặc định (đổi được từ xa) ──────────────────────────────────────
// Node cống:  m = H - d ;  L(%) = m / Hmax * 100
#define DEFAULT_H_CM        60.0f   // khoảng cách cảm biến → đáy ống (cm)
#define DEFAULT_HMAX_CM     35.0f   // chiều sâu dùng để tính % đầy (cm)
// Node đường: h = d0 - d
#define DEFAULT_D0_CM       40.0f   // khoảng cách cảm biến → mặt khay khi khô (cm)

// Ngưỡng node đường (cm) — giá trị theo báo cáo; khi demo trên khay nông
// nên quy đổi nhỏ lại (ví dụ 2 / 4 / 3 / 1.5) bằng form cấu hình trên dashboard.
#define DEFAULT_T_YELLOW      10.0f // h ≥ t_yellow           → VÀNG
#define DEFAULT_T_RED         20.0f // h > t_red              → ĐỎ
#define DEFAULT_T_RED_LOW     15.0f // đang ĐỎ, chỉ về VÀNG khi h < t_red_low
#define DEFAULT_T_YELLOW_LOW   8.0f // đang VÀNG, chỉ về XANH khi h < t_yellow_low
#define DEFAULT_STABLE_S        20  // mức mới phải giữ ổn định ≥ N giây mới được công nhận

#define DEFAULT_PERIOD_MS     2000  // chu kỳ đo (ms)
