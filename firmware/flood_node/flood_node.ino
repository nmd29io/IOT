/**
 * flood_node.ino — Firmware dùng chung cho mọi node giám sát ngập (ESP32)
 *
 * Hai node chạy CÙNG một chương trình, chỉ khác NODE_ID / NODE_ROLE trong config.h:
 *   ROLE_DRAIN (cong1) : chỉ đo, lọc trung vị và gửi mực nước lên MQTT.
 *                        Tốc độ dâng & phân mức cảnh báo do Node-RED (Edge IT) xử lý.
 *   ROLE_ROAD  (duong1): đo độ sâu ngập, TỰ phân mức (hysteresis + thời gian giữ)
 *                        và điều khiển đèn/còi. Phần này chạy trong một FreeRTOS task
 *                        riêng nên vẫn hoạt động khi mất WiFi hoặc mất broker.
 *
 * ── MQTT topics (<id> = NODE_ID) ─────────────────────────────────────────────
 *   Node → Edge
 *     flood/<id>/level      dữ liệu đo (JSON)
 *                           cống : {"node","d","m","L","ts"}
 *                           đường: {"node","d","h","muc","stable","buzzer_muted","ts"}
 *     flood/<id>/status     "online" | "offline"  (retained, Last Will)
 *     flood/<id>/info       heartbeat thiết bị (retained): fw, ip, rssi, uptime, cfg...
 *     flood/<id>/event      sự kiện: calibrated, config_applied, config_rejected, buzzer...
 *   Edge → Node
 *     flood/<id>/config     cấu hình từ xa (JSON, retained) — chỉ cần gửi các khoá muốn đổi
 *                           {"H","Hmax","d0","t_yellow","t_red","t_red_low","t_yellow_low",
 *                            "period_s","stable_s"}
 *     flood/<id>/cmd        {"cmd":"calibrate"|"report"|"reboot"}
 *     flood/<id>/alarm/cmd  "off" (tắt còi) | "on" (bật lại còi)   — chỉ node đường
 *
 * Thư viện: PubSubClient (Nick O'Leary), ArduinoJson ≥ 7 (Benoit Blanchon).
 * Board   : ESP32 Dev Module (arduino-esp32 core ≥ 2.0).
 */

#define ROLE_DRAIN 1
#define ROLE_ROAD  2

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"   // sao chép từ config.example.h

#if !defined(NODE_ROLE) || (NODE_ROLE != ROLE_DRAIN && NODE_ROLE != ROLE_ROAD)
#error "Hãy đặt NODE_ROLE là ROLE_DRAIN hoặc ROLE_ROAD trong config.h"
#endif

#define FW_VERSION          "2.0.0"

// ─── Cảm biến ────────────────────────────────────────────────────────────────
#define MEDIAN_SAMPLES      7
#define MIN_VALID_SAMPLES   4        // cần ≥ 4/7 mẫu hợp lệ mới chấp nhận phép đo
#define SAMPLE_GAP_MS       60       // JSN-SR04T cần ≥ 50 ms giữa hai lần phát
#define ECHO_TIMEOUT_US     26000UL  // ≈ 4,5 m
#define MIN_RANGE_CM        20.0f    // vùng mù của JSN-SR04T
#define MAX_RANGE_CM        450.0f

// ─── Mạng ────────────────────────────────────────────────────────────────────
#define WIFI_RETRY_MS       15000UL
#define MQTT_RETRY_MS       5000UL
#define INFO_INTERVAL_MS    30000UL

enum Level : uint8_t { GREEN = 0, YELLOW = 1, RED = 2 };
static const char* LEVEL_STR[] = { "green", "yellow", "red" };

// ─── Cấu hình (đổi được từ xa, lưu NVS) ─────────────────────────────────────
struct Config {
    float    H, Hmax, d0;
    float    tYellow, tRed, tRedLow, tYellowLow;
    uint32_t periodMs, stableMs;
};
static Config cfg;
static portMUX_TYPE cfgMux = portMUX_INITIALIZER_UNLOCKED;
static Preferences prefs;

static Config getCfg() {
    portENTER_CRITICAL(&cfgMux);
    Config c = cfg;
    portEXIT_CRITICAL(&cfgMux);
    return c;
}

static void setCfg(const Config& c) {
    portENTER_CRITICAL(&cfgMux);
    cfg = c;
    portEXIT_CRITICAL(&cfgMux);
}

static void loadConfig() {
    prefs.begin("flood", true);
    Config c;
    c.H          = prefs.getFloat("H",     DEFAULT_H_CM);
    c.Hmax       = prefs.getFloat("Hmax",  DEFAULT_HMAX_CM);
    c.d0         = prefs.getFloat("d0",    DEFAULT_D0_CM);
    c.tYellow    = prefs.getFloat("tY",    DEFAULT_T_YELLOW);
    c.tRed       = prefs.getFloat("tR",    DEFAULT_T_RED);
    c.tRedLow    = prefs.getFloat("tRl",   DEFAULT_T_RED_LOW);
    c.tYellowLow = prefs.getFloat("tYl",   DEFAULT_T_YELLOW_LOW);
    c.periodMs   = prefs.getUInt ("per",   DEFAULT_PERIOD_MS);
    c.stableMs   = prefs.getUInt ("stab",  DEFAULT_STABLE_S * 1000UL);
    prefs.end();
    setCfg(c);
}

static void saveConfig(const Config& c) {
    prefs.begin("flood", false);
    prefs.putFloat("H",    c.H);
    prefs.putFloat("Hmax", c.Hmax);
    prefs.putFloat("d0",   c.d0);
    prefs.putFloat("tY",   c.tYellow);
    prefs.putFloat("tR",   c.tRed);
    prefs.putFloat("tRl",  c.tRedLow);
    prefs.putFloat("tYl",  c.tYellowLow);
    prefs.putUInt ("per",  c.periodMs);
    prefs.putUInt ("stab", c.stableMs);
    prefs.end();
}

// ─── Trạng thái dùng chung giữa task đo và task mạng ────────────────────────
static portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
struct Sample {
    float d, value;          // value = m (cống) hoặc h (đường)
    float fill;              // L (%) — chỉ node cống
    Level level;             // chỉ node đường
    bool  stable, muted;
};
static Sample   lastSample;
static volatile bool     newSample        = false;
static volatile uint32_t sensorErrors     = 0;
static volatile bool     calibrateRequest = false;
static volatile int8_t   calibrateResult  = 0;   // 1 = ok, -1 = lỗi, 0 = chưa có
static volatile float    calibrateValue   = 0.0f;
static volatile bool     buzzerMuted      = false;
static volatile bool     buzzerEvent      = false;

// ─── Topic ───────────────────────────────────────────────────────────────────
static String tLevel, tStatus, tInfo, tEvent, tConfig, tCmd, tAlarm;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

static uint32_t lastWifiTry = 0, lastMqttTry = 0, lastInfo = 0, lastMeasure = 0;
static bool     ntpStarted  = false;

// ═════════════════════════════════════════════════════════════════════════════
//  Đo khoảng cách: 7 lần, bỏ mẫu lỗi / trong vùng mù, lấy trung vị.
//  Trả về NAN nếu không đủ mẫu hợp lệ (không gán giá trị giả như 400 cm).
// ═════════════════════════════════════════════════════════════════════════════
static float measureDistanceCm() {
    float r[MEDIAN_SAMPLES];
    uint8_t n = 0;

    for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++) {
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(20);           // JSN-SR04T ổn định hơn với xung ≥ 20 µs
        digitalWrite(TRIG_PIN, LOW);

        unsigned long us = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
        if (us > 0) {
            float d = us * 0.0343f / 2.0f;
            if (d >= MIN_RANGE_CM && d <= MAX_RANGE_CM) r[n++] = d;
        }
        delay(SAMPLE_GAP_MS);
    }

    if (n < MIN_VALID_SAMPLES) return NAN;

    for (uint8_t i = 1; i < n; i++) {          // insertion sort
        float k = r[i];
        int8_t j = i - 1;
        while (j >= 0 && r[j] > k) { r[j + 1] = r[j]; j--; }
        r[j + 1] = k;
    }
    return (n % 2) ? r[n / 2] : (r[n / 2 - 1] + r[n / 2]) / 2.0f;
}

static float round1(float x) { return roundf(x * 10.0f) / 10.0f; }

static uint32_t epochNow() {
    time_t t = time(nullptr);
    return (t > 1700000000) ? (uint32_t)t : 0;   // 0 = chưa đồng bộ NTP
}

// ═════════════════════════════════════════════════════════════════════════════
//  NODE ĐƯỜNG — phân mức, đèn, còi (chạy độc lập với mạng)
// ═════════════════════════════════════════════════════════════════════════════
#if NODE_ROLE == ROLE_ROAD

static Level    level        = GREEN;
static Level    pending      = GREEN;
static uint32_t pendingSince = 0;

// Hysteresis: lên mức dùng ngưỡng trên, xuống mức dùng ngưỡng dưới.
static Level classify(float h, Level cur, const Config& c) {
    Level target = (h > c.tRed) ? RED : (h >= c.tYellow) ? YELLOW : GREEN;
    if (target < cur) {
        if (cur == RED && h >= c.tRedLow)                      target = RED;
        else if (target == GREEN && h >= c.tYellowLow)          target = YELLOW;
    }
    return target;
}

static void setLeds(Level l) {
    digitalWrite(LED_GREEN_PIN,  l == GREEN  ? HIGH : LOW);
    digitalWrite(LED_YELLOW_PIN, l == YELLOW ? HIGH : LOW);
    digitalWrite(LED_RED_PIN,    l == RED    ? HIGH : LOW);
}

static void buzzerWrite(bool on) {
#if BUZZER_PASSIVE
    if (on) tone(BUZZER_PIN, 2500); else noTone(BUZZER_PIN);
#else
    digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
#endif
}

// Còi chỉ kêu ở mức ĐỎ, kêu ngắt quãng 250 ms.
static void buzzerTick() {
    static bool     on = false;
    static uint32_t lastToggle = 0;
    bool shouldBeep = (level == RED) && !buzzerMuted;
    if (!shouldBeep) {
        if (on) { buzzerWrite(false); on = false; }
        return;
    }
    if (millis() - lastToggle >= 250) {
        lastToggle = millis();
        on = !on;
        buzzerWrite(on);
    }
}

static void localAlarmTask(void*) {
    for (;;) {
        uint32_t t0 = millis();
        Config c = getCfg();

        if (calibrateRequest) {                      // hiệu chuẩn d0 khi đường khô
            float d = measureDistanceCm();
            if (!isnan(d)) {
                c.d0 = d;
                setCfg(c);
                calibrateValue = d;
                calibrateResult = 1;
            } else {
                calibrateResult = -1;
            }
            calibrateRequest = false;
        }

        float d = measureDistanceCm();
        if (isnan(d)) {
            sensorErrors++;
        } else {
            float h = c.d0 - d;
            if (h < 0) h = 0;

            // Lọc theo thời gian ổn định: xe/người đi qua chỉ làm mức đổi vài giây → bị bỏ qua.
            Level cand = classify(h, level, c);
            uint32_t now = millis();
            if (cand == level) {
                pending = level;
            } else if (cand != pending) {
                pending = cand;
                pendingSince = now;
            } else if (now - pendingSince >= c.stableMs) {
                level = cand;
                if (level != RED && buzzerMuted) {   // hết ĐỎ → lần ĐỎ sau còi kêu lại
                    buzzerMuted = false;
                    buzzerEvent = true;
                }
            }
            setLeds(level);

            portENTER_CRITICAL(&dataMux);
            lastSample.d      = d;
            lastSample.value  = h;
            lastSample.fill   = 0;
            lastSample.level  = level;
            lastSample.stable = (pending == level);
            lastSample.muted  = buzzerMuted;
            newSample = true;
            portEXIT_CRITICAL(&dataMux);
        }

        while (millis() - t0 < c.periodMs) {
            buzzerTick();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

#endif  // ROLE_ROAD

// ═════════════════════════════════════════════════════════════════════════════
//  NODE CỐNG — chỉ đo & gửi (Node-RED tính tốc độ dâng, phân mức)
// ═════════════════════════════════════════════════════════════════════════════
#if NODE_ROLE == ROLE_DRAIN
static void drainMeasure() {
    Config c = getCfg();

    if (calibrateRequest) {                          // hiệu chuẩn H khi cống rỗng
        float d = measureDistanceCm();
        if (!isnan(d)) {
            c.H = d;
            if (c.Hmax > c.H) c.Hmax = c.H;
            setCfg(c);
            calibrateValue = d;
            calibrateResult = 1;
        } else {
            calibrateResult = -1;
        }
        calibrateRequest = false;
    }

    float d = measureDistanceCm();
    if (isnan(d)) { sensorErrors++; return; }

    float m = constrain(c.H - d, 0.0f, c.H);
    float L = constrain(m / c.Hmax * 100.0f, 0.0f, 100.0f);

    portENTER_CRITICAL(&dataMux);
    lastSample.d     = d;
    lastSample.value = m;
    lastSample.fill  = L;
    newSample = true;
    portEXIT_CRITICAL(&dataMux);
}
#endif

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT — publish
// ═════════════════════════════════════════════════════════════════════════════
static void publishJson(const String& topic, JsonDocument& doc, bool retain = false) {
    if (!mqtt.connected()) return;
    char buf[640];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqtt.publish(topic.c_str(), (const uint8_t*)buf, n, retain);
}

static void publishEvent(const char* ev, const char* key = nullptr, float value = NAN,
                         const char* reason = nullptr) {
    JsonDocument doc;
    doc["node"]  = NODE_ID;
    doc["event"] = ev;
    if (key)            doc["key"]    = key;
    if (!isnan(value))  doc["value"]  = round1(value);
    if (reason)         doc["reason"] = reason;
    doc["ts"] = epochNow();
    publishJson(tEvent, doc);
}

static void publishInfo() {
    Config c = getCfg();
    JsonDocument doc;
    doc["node"]       = NODE_ID;
    doc["role"]       = (NODE_ROLE == ROLE_DRAIN) ? "drain" : "road";
    doc["fw"]         = FW_VERSION;
    doc["ip"]         = WiFi.localIP().toString();
    doc["rssi"]       = WiFi.RSSI();
    doc["uptime_s"]   = millis() / 1000;
    doc["heap"]       = ESP.getFreeHeap();
    doc["sensor_err"] = (uint32_t)sensorErrors;
    JsonObject jc = doc["cfg"].to<JsonObject>();
#if NODE_ROLE == ROLE_DRAIN
    jc["H"]    = round1(c.H);
    jc["Hmax"] = round1(c.Hmax);
#else
    jc["d0"]           = round1(c.d0);
    jc["t_yellow"]     = c.tYellow;
    jc["t_red"]        = c.tRed;
    jc["t_red_low"]    = c.tRedLow;
    jc["t_yellow_low"] = c.tYellowLow;
    jc["stable_s"]     = c.stableMs / 1000;
#endif
    jc["period_s"] = c.periodMs / 1000.0f;
    doc["ts"] = epochNow();
    publishJson(tInfo, doc, true);
}

static void publishSample() {
    Sample s;
    portENTER_CRITICAL(&dataMux);
    if (!newSample) { portEXIT_CRITICAL(&dataMux); return; }
    s = lastSample;
    newSample = false;
    portEXIT_CRITICAL(&dataMux);

    JsonDocument doc;
    doc["node"] = NODE_ID;
    doc["d"]    = round1(s.d);
#if NODE_ROLE == ROLE_DRAIN
    doc["m"] = round1(s.value);
    doc["L"] = round1(s.fill);
#else
    doc["h"]            = round1(s.value);
    doc["muc"]          = LEVEL_STR[s.level];
    doc["stable"]       = s.stable;
    doc["buzzer_muted"] = s.muted;
#endif
    doc["ts"] = epochNow();
    publishJson(tLevel, doc);
    Serial.printf("[PUB] %s d=%.1f value=%.1f\n", tLevel.c_str(), s.d, s.value);
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT — nhận cấu hình & lệnh
// ═════════════════════════════════════════════════════════════════════════════
static bool readNum(JsonObject o, const char* k, float& out) {
    if (!o[k].is<float>()) return false;
    out = o[k].as<float>();
    return true;
}

static void applyConfig(JsonObject o) {
    Config c = getCfg();
    float v;
    if (readNum(o, "H", v))            c.H          = v;
    if (readNum(o, "Hmax", v))         c.Hmax       = v;
    if (readNum(o, "d0", v))           c.d0         = v;
    if (readNum(o, "t_yellow", v))     c.tYellow    = v;
    if (readNum(o, "t_red", v))        c.tRed       = v;
    if (readNum(o, "t_red_low", v))    c.tRedLow    = v;
    if (readNum(o, "t_yellow_low", v)) c.tYellowLow = v;
    if (readNum(o, "period_s", v))     c.periodMs   = (uint32_t)(v * 1000.0f);
    if (readNum(o, "stable_s", v))     c.stableMs   = (uint32_t)(v * 1000.0f);

    const char* err = nullptr;
    if (c.H < MIN_RANGE_CM || c.H > MAX_RANGE_CM)              err = "H ngoai khoang 20-450 cm";
    else if (c.Hmax <= 0 || c.Hmax > c.H)                      err = "Hmax phai trong (0, H]";
    else if (c.d0 < MIN_RANGE_CM || c.d0 > MAX_RANGE_CM)       err = "d0 ngoai khoang 20-450 cm";
    else if (!(c.tYellow > 0 && c.tYellow < c.tRed))           err = "can 0 < t_yellow < t_red";
    else if (!(c.tRedLow >= c.tYellow && c.tRedLow < c.tRed))  err = "can t_yellow <= t_red_low < t_red";
    else if (!(c.tYellowLow >= 0 && c.tYellowLow < c.tYellow)) err = "can 0 <= t_yellow_low < t_yellow";
    else if (c.periodMs < 1000 || c.periodMs > 60000)          err = "period_s trong 1-60";
    else if (c.stableMs > 300000)                              err = "stable_s toi da 300";

    if (err) {
        Serial.printf("[CFG] Từ chối: %s\n", err);
        publishEvent("config_rejected", nullptr, NAN, err);
        return;
    }
    setCfg(c);
    saveConfig(c);
    Serial.println("[CFG] Đã áp dụng cấu hình mới");
    publishEvent("config_applied");
    publishInfo();
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
    String t(topic);
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim();
    Serial.printf("[SUB] %s → %s\n", topic, msg.c_str());

#if NODE_ROLE == ROLE_ROAD
    if (t == tAlarm) {
        if (msg == "off" && lastSample.level == RED) buzzerMuted = true;
        else if (msg == "on")                         buzzerMuted = false;
        buzzerEvent = true;
        return;
    }
#endif

    if (t == tConfig) {
        if (len == 0) return;                         // retained bị xoá
        JsonDocument doc;
        if (deserializeJson(doc, msg) || !doc.is<JsonObject>()) {
            publishEvent("config_rejected", nullptr, NAN, "JSON khong hop le");
            return;
        }
        applyConfig(doc.as<JsonObject>());
        return;
    }

    if (t == tCmd) {
        String cmd = msg;
        JsonDocument doc;
        if (!deserializeJson(doc, msg) && doc["cmd"].is<const char*>()) cmd = doc["cmd"].as<const char*>();
        if (cmd == "calibrate")      calibrateRequest = true;
        else if (cmd == "report")    publishInfo();
        else if (cmd == "reboot")  { publishEvent("rebooting"); mqtt.loop(); delay(200); ESP.restart(); }
        else publishEvent("unknown_cmd", nullptr, NAN, cmd.c_str());
    }
}

static void mqttConnect() {
    String clientId = String("esp32-") + NODE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = (strlen(MQTT_USER) > 0)
        ? mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, tStatus.c_str(), 1, true, "offline")
        : mqtt.connect(clientId.c_str(), tStatus.c_str(), 1, true, "offline");
    if (!ok) {
        Serial.printf("[MQTT] Kết nối thất bại, rc=%d\n", mqtt.state());
        return;
    }
    Serial.println("[MQTT] Đã kết nối broker");
    mqtt.publish(tStatus.c_str(), "online", true);
    mqtt.subscribe(tConfig.c_str(), 1);
    mqtt.subscribe(tCmd.c_str(), 1);
#if NODE_ROLE == ROLE_ROAD
    mqtt.subscribe(tAlarm.c_str(), 1);
#endif
    publishInfo();
    lastInfo = millis();
}

// Không chặn: mất WiFi/broker chỉ làm hàm này return sớm, phần đo vẫn chạy.
static void networkLoop() {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiTry >= WIFI_RETRY_MS) {
            lastWifiTry = now;
            Serial.println("[WiFi] Đang kết nối lại…");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        return;
    }
    if (!ntpStarted) {
        configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");   // GMT+7
        ntpStarted = true;
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    }
    if (!mqtt.connected()) {
        if (now - lastMqttTry >= MQTT_RETRY_MS) {
            lastMqttTry = now;
            mqttConnect();
        }
        return;
    }
    mqtt.loop();
}

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== Flood node %s (fw %s) ===\n", NODE_ID, FW_VERSION);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    loadConfig();

    String base = String("flood/") + NODE_ID + "/";
    tLevel  = base + "level";
    tStatus = base + "status";
    tInfo   = base + "info";
    tEvent  = base + "event";
    tConfig = base + "config";
    tCmd    = base + "cmd";
    tAlarm  = base + "alarm/cmd";

#if NODE_ROLE == ROLE_ROAD
    pinMode(LED_GREEN_PIN,  OUTPUT);
    pinMode(LED_YELLOW_PIN, OUTPUT);
    pinMode(LED_RED_PIN,    OUTPUT);
    pinMode(BUZZER_PIN,     OUTPUT);
    buzzerWrite(false);
    setLeds(GREEN);
    // Task cảnh báo tại chỗ: ưu tiên cao hơn loop(), không phụ thuộc mạng.
    xTaskCreatePinnedToCore(localAlarmTask, "localAlarm", 4096, nullptr, 2, nullptr, 1);
#endif

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiTry = millis();

    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(768);
    mqtt.setSocketTimeout(3);
}

void loop() {
    networkLoop();

#if NODE_ROLE == ROLE_DRAIN
    if (millis() - lastMeasure >= getCfg().periodMs) {
        lastMeasure = millis();
        drainMeasure();
    }
#endif

    publishSample();

    if (calibrateResult != 0) {                     // báo kết quả hiệu chuẩn
        Config c = getCfg();
        if (calibrateResult > 0) {
            saveConfig(c);
            publishEvent("calibrated", (NODE_ROLE == ROLE_DRAIN) ? "H" : "d0", calibrateValue);
            publishInfo();
        } else {
            publishEvent("calibrate_failed", nullptr, NAN, "khong do duoc khoang cach");
        }
        calibrateResult = 0;
    }

#if NODE_ROLE == ROLE_ROAD
    if (buzzerEvent) {
        buzzerEvent = false;
        JsonDocument doc;
        doc["node"]  = NODE_ID;
        doc["event"] = "buzzer";
        doc["muted"] = (bool)buzzerMuted;
        doc["ts"]    = epochNow();
        publishJson(tEvent, doc);
    }
#endif

    if (mqtt.connected() && millis() - lastInfo >= INFO_INTERVAL_MS) {
        lastInfo = millis();
        publishInfo();
    }
    delay(10);
}
