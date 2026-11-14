/**
 * config.wokwi.h — cấu hình chạy firmware flood_node trên mô phỏng Wokwi.
 * Trên wokwi.com: tạo file tên  config.h  trong project và dán nội dung này vào.
 *
 * ESP32 ảo không nhìn thấy laptop (localhost / 10.x.x.x) → kết nối broker công cộng,
 * laptop bridge broker đó về Mosquitto cục bộ (mosquitto/conf.d/wokwi-bridge.conf).
 * Không có mật khẩu nào ở đây: Wokwi-GUEST là WiFi ảo công khai của Wokwi.
 */
#pragma once

#define NODE_MODE   MODE_DUAL
#define DRAIN_ID    "cong1"
#define ROAD_ID     "duong1"

#define WIFI_SSID       "Wokwi-GUEST"
#define WIFI_PASSWORD   ""
#define WIFI_CHANNEL    6

#define MQTT_SERVER     "broker.hivemq.com"
#define MQTT_PORT       1883
#define MQTT_USER       ""
#define MQTT_PASSWORD   ""
// Tiền tố riêng của nhóm trên broker công cộng — phải GIỐNG trong wokwi-bridge.conf
#define TOPIC_PREFIX    "uet-int3323-n1-09e061/"

// Chân (khớp diagram.json). Trên Wokwi dùng HC-SR04 (cùng giao thức với JSN-SR04T).
#define SENSOR1_TRIG    5
#define SENSOR1_ECHO    18
#define SENSOR2_TRIG    19
#define SENSOR2_ECHO    21
#define LED_GREEN_PIN   26
#define LED_YELLOW_PIN  27
#define LED_RED_PIN     14
#define BUZZER_PIN      25
#define BUZZER_PASSIVE  1           // còi của Wokwi là loại thụ động → cần phát tần số

// Kéo thanh trượt khoảng cách của từng cảm biến (click vào cảm biến khi đang chạy):
//   Cống  (cảm biến trên): d = 60 cm → cống rỗng; d = 25 cm → m = 35 cm = 100%
//   Đường (cảm biến dưới): d = 50 cm → khô; d = 38 cm → ngập 12 cm (VÀNG); d = 25 cm → 25 cm (ĐỎ)
#define DEFAULT_H_CM        60.0f
#define DEFAULT_HMAX_CM     35.0f
#define DEFAULT_D0_CM       50.0f
#define DEFAULT_T_YELLOW      10.0f
#define DEFAULT_T_RED         20.0f
#define DEFAULT_T_RED_LOW     15.0f
#define DEFAULT_T_YELLOW_LOW   8.0f
#define DEFAULT_STABLE_S        5   // rút ngắn để thử nhanh trên mô phỏng (thật: 20 s)
#define DEFAULT_PERIOD_MS     2000
