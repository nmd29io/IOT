/**
 * flood_node.ino — Firmware dùng chung cho node giám sát ngập (ESP32)
 *
 * Mỗi "kênh" = 1 node logic = 1 cảm biến JSN-SR04T + 1 kết nối MQTT riêng (Last Will riêng)
 * + 1 bộ cấu hình riêng (NVS). Chọn trong config.h:
 *   MODE_DUAL : 1 ESP32, 2 cảm biến → 2 node logic cong1 + duong1 (phần cứng hiện có của nhóm)
 *   MODE_DRAIN: 1 ESP32 làm node cống     MODE_ROAD: 1 ESP32 làm node đường
 * Phía Node-RED / cloud không phân biệt: luôn thấy flood/cong1/... và flood/duong1/...
 *
 * Vai trò:
 *   Node cống : chỉ đo, lọc trung vị, gửi mực nước. Tốc độ dâng & phân mức do Node-RED (Edge) xử lý.
 *   Node đường: đo, TỰ phân mức (hysteresis + giữ ổn định) và điều khiển đèn/còi.
 * Toàn bộ phần đo + đèn/còi chạy trong một FreeRTOS task riêng → vẫn hoạt động khi mất WiFi/broker.
 * Hai cảm biến được đo LẦN LƯỢT (không phát sóng cùng lúc) để tránh nhiễu chéo.
 *
 * ── MQTT topics (<id> = mã node) ─────────────────────────────────────────────
 *   Node → Edge
 *     flood/<id>/level      cống : {"node","d","m","L","ts"}
 *                           đường: {"node","d","h","muc","stable","buzzer_muted","ts"}
 *     flood/<id>/status     "online" | "offline"  (retained, Last Will)
 *     flood/<id>/info       heartbeat (retained): fw, ip, rssi, uptime, lỗi đo, cfg...
 *     flood/<id>/event      calibrated, config_applied, config_rejected, buzzer...
 *   Edge → Node
 *     flood/<id>/config     {"H","Hmax","d0","t_yellow","t_red","t_red_low","t_yellow_low","period_s","stable_s"}
 *     flood/<id>/cmd        {"cmd":"calibrate"|"report"|"reboot"}
 *     flood/<id>/alarm/cmd  "off" | "on"   (chỉ node đường)
 *
 * Thư viện: PubSubClient (Nick O'Leary), ArduinoJson ≥ 7 (Benoit Blanchon).
 * Board   : ESP32 Dev Module (arduino-esp32 core ≥ 2.0).
 */

#define MODE_DRAIN 1
#define MODE_ROAD  2
#define MODE_DUAL  3

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"   // sao chép từ config.example.h

#if !defined(NODE_MODE) || (NODE_MODE != MODE_DRAIN && NODE_MODE != MODE_ROAD && NODE_MODE != MODE_DUAL)
#error "Đặt NODE_MODE là MODE_DUAL, MODE_DRAIN hoặc MODE_ROAD trong config.h"
#endif

#define FW_VERSION          "2.1.0"

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

enum Role  : uint8_t { DRAIN = 0, ROAD = 1 };
enum Level : uint8_t { GREEN = 0, YELLOW = 1, RED = 2 };
static const char* LEVEL_STR[] = { "green", "yellow", "red" };

struct Config {
    float    H, Hmax, d0;
    float    tYellow, tRed, tRedLow, tYellowLow;
    uint32_t periodMs, stableMs;
};

struct Sample { float d, value, fill; Level level; bool stable, muted; };

struct Channel {
    const char* id;
    Role        role;
    uint8_t     trig, echo;
    Config      cfg;
    String      tLevel, tStatus, tInfo, tEvent, tConfig, tCmd, tAlarm;
    WiFiClient  net;
    PubSubClient mqtt;
    uint32_t    lastMqttTry = 0, lastInfo = 0, lastMeasure = 0;
    // giao tiếp task đo ↔ loop()
    Sample      sample;
    volatile bool     newSample = false;
    volatile uint32_t sensorErrors = 0;
    volatile bool     calibrateRequest = false;
    volatile int8_t   calibrateResult = 0;   // 1 ok, -1 lỗi
    volatile float    calibrateValue = 0;
    // trạng thái node đường
    Level       level = GREEN, pending = GREEN;
    uint32_t    pendingSince = 0;
    volatile bool muted = false, buzzerEvent = false;
};

#if NODE_MODE == MODE_DUAL
static const uint8_t N_CH = 2;
#else
static const uint8_t N_CH = 1;
#endif
static Channel ch[N_CH];
static Channel* roadCh = nullptr;              // kênh điều khiển đèn/còi (nếu có)
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Preferences prefs;
static uint32_t lastWifiTry = 0;
static bool ntpStarted = false;

// ═════════════════════════════════════════════════════════════════════════════
//  Cấu hình (NVS, mỗi kênh một namespace = mã node)
// ═════════════════════════════════════════════════════════════════════════════
static Config getCfg(Channel& c) {
    portENTER_CRITICAL(&mux); Config k = c.cfg; portEXIT_CRITICAL(&mux);
    return k;
}
static void setCfg(Channel& c, const Config& k) {
    portENTER_CRITICAL(&mux); c.cfg = k; portEXIT_CRITICAL(&mux);
}
static void loadConfig(Channel& c) {
    prefs.begin(c.id, true);
    Config k;
    k.H          = prefs.getFloat("H",    DEFAULT_H_CM);
    k.Hmax       = prefs.getFloat("Hmax", DEFAULT_HMAX_CM);
    k.d0         = prefs.getFloat("d0",   DEFAULT_D0_CM);
    k.tYellow    = prefs.getFloat("tY",   DEFAULT_T_YELLOW);
    k.tRed       = prefs.getFloat("tR",   DEFAULT_T_RED);
    k.tRedLow    = prefs.getFloat("tRl",  DEFAULT_T_RED_LOW);
    k.tYellowLow = prefs.getFloat("tYl",  DEFAULT_T_YELLOW_LOW);
    k.periodMs   = prefs.getUInt ("per",  DEFAULT_PERIOD_MS);
    k.stableMs   = prefs.getUInt ("stab", DEFAULT_STABLE_S * 1000UL);
    prefs.end();
    setCfg(c, k);
}
static void saveConfig(Channel& c, const Config& k) {
    prefs.begin(c.id, false);
    prefs.putFloat("H", k.H);        prefs.putFloat("Hmax", k.Hmax);  prefs.putFloat("d0", k.d0);
    prefs.putFloat("tY", k.tYellow); prefs.putFloat("tR", k.tRed);
    prefs.putFloat("tRl", k.tRedLow); prefs.putFloat("tYl", k.tYellowLow);
    prefs.putUInt("per", k.periodMs); prefs.putUInt("stab", k.stableMs);
    prefs.end();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Đo khoảng cách: 7 lần, bỏ mẫu lỗi / vùng mù, lấy trung vị. NAN nếu không đủ mẫu.
// ═════════════════════════════════════════════════════════════════════════════
static float measureDistanceCm(uint8_t trig, uint8_t echo) {
    float r[MEDIAN_SAMPLES];
    uint8_t n = 0;
    for (uint8_t i = 0; i < MEDIAN_SAMPLES; i++) {
        digitalWrite(trig, LOW);
        delayMicroseconds(2);
        digitalWrite(trig, HIGH);
        delayMicroseconds(20);
        digitalWrite(trig, LOW);
        unsigned long us = pulseIn(echo, HIGH, ECHO_TIMEOUT_US);
        if (us > 0) {
            float d = us * 0.0343f / 2.0f;
            if (d >= MIN_RANGE_CM && d <= MAX_RANGE_CM) r[n++] = d;
        }
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_GAP_MS));
    }
    if (n < MIN_VALID_SAMPLES) return NAN;
    for (uint8_t i = 1; i < n; i++) {                 // insertion sort
        float k = r[i]; int8_t j = i - 1;
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
//  Node đường: phân mức + đèn/còi
// ═════════════════════════════════════════════════════════════════════════════
static Level classify(float h, Level cur, const Config& k) {
    Level target = (h > k.tRed) ? RED : (h >= k.tYellow) ? YELLOW : GREEN;
    if (target < cur) {                                // hạ mức dùng ngưỡng thấp hơn (hysteresis)
        if (cur == RED && h >= k.tRedLow)              target = RED;
        else if (target == GREEN && h >= k.tYellowLow) target = YELLOW;
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
static void buzzerTick() {                          // còi chỉ kêu ở mức ĐỎ, ngắt quãng 250 ms
    static bool on = false;
    static uint32_t lastToggle = 0;
    bool beep = roadCh && roadCh->level == RED && !roadCh->muted;
    if (!beep) { if (on) { buzzerWrite(false); on = false; } return; }
    if (millis() - lastToggle >= 250) { lastToggle = millis(); on = !on; buzzerWrite(on); }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Một lần đo cho một kênh (chạy trong task đo)
// ═════════════════════════════════════════════════════════════════════════════
static void measureChannel(Channel& c) {
    Config k = getCfg(c);

    if (c.calibrateRequest) {        // cống: đo lại H khi ống rỗng; đường: đo lại d0 khi khô
        float d = measureDistanceCm(c.trig, c.echo);
        if (!isnan(d)) {
            if (c.role == DRAIN) { k.H = d; if (k.Hmax > k.H) k.Hmax = k.H; }
            else                 { k.d0 = d; }
            setCfg(c, k);
            c.calibrateValue = d;
            c.calibrateResult = 1;
        } else {
            c.calibrateResult = -1;
        }
        c.calibrateRequest = false;
    }

    float d = measureDistanceCm(c.trig, c.echo);
    if (isnan(d)) { c.sensorErrors++; return; }

    Sample s = {};
    s.d = d;
    if (c.role == DRAIN) {
        float m = constrain(k.H - d, 0.0f, k.H);
        s.value = m;
        s.fill  = constrain(m / k.Hmax * 100.0f, 0.0f, 100.0f);
    } else {
        float h = k.d0 - d;
        if (h < 0) h = 0;
        s.value = h;
        // Lọc theo thời gian ổn định: xe/người đi qua chỉ làm mức đổi vài giây → bị bỏ qua.
        Level cand = classify(h, c.level, k);
        uint32_t now = millis();
        if (cand == c.level)          c.pending = c.level;
        else if (cand != c.pending) { c.pending = cand; c.pendingSince = now; }
        else if (now - c.pendingSince >= k.stableMs) {
            c.level = cand;
            if (c.level != RED && c.muted) { c.muted = false; c.buzzerEvent = true; }  // lần ĐỎ sau còi kêu lại
        }
        setLeds(c.level);
        s.level  = c.level;
        s.stable = (c.pending == c.level);
        s.muted  = c.muted;
    }
    portENTER_CRITICAL(&mux);
    c.sample = s;
    c.newSample = true;
    portEXIT_CRITICAL(&mux);
}

// Task đo: chạy độc lập với mạng. Các kênh được đo lần lượt (không phát sóng cùng lúc).
static void sensorTask(void*) {
    for (;;) {
        for (uint8_t i = 0; i < N_CH; i++) {
            Channel& c = ch[i];
            if (c.calibrateRequest || millis() - c.lastMeasure >= getCfg(c).periodMs) {
                c.lastMeasure = millis();
                measureChannel(c);
            }
        }
        buzzerTick();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT — publish
// ═════════════════════════════════════════════════════════════════════════════
static void publishJson(Channel& c, const String& topic, JsonDocument& doc, bool retain = false) {
    if (!c.mqtt.connected()) return;
    char buf[640];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    c.mqtt.publish(topic.c_str(), (const uint8_t*)buf, n, retain);
}

static void publishEvent(Channel& c, const char* ev, const char* key = nullptr, float value = NAN,
                         const char* reason = nullptr) {
    JsonDocument doc;
    doc["node"] = c.id;
    doc["event"] = ev;
    if (key)           doc["key"] = key;
    if (!isnan(value)) doc["value"] = round1(value);
    if (reason)        doc["reason"] = reason;
    doc["ts"] = epochNow();
    publishJson(c, c.tEvent, doc);
}

static void publishInfo(Channel& c) {
    Config k = getCfg(c);
    JsonDocument doc;
    doc["node"]       = c.id;
    doc["role"]       = (c.role == DRAIN) ? "drain" : "road";
    doc["fw"]         = FW_VERSION;
    doc["board"]      = (N_CH > 1) ? "esp32-dual" : "esp32";   // 2 node logic chung 1 mạch
    doc["ip"]         = WiFi.localIP().toString();
    doc["rssi"]       = WiFi.RSSI();
    doc["uptime_s"]   = millis() / 1000;
    doc["heap"]       = ESP.getFreeHeap();
    doc["sensor_err"] = (uint32_t)c.sensorErrors;
    JsonObject jc = doc["cfg"].to<JsonObject>();
    if (c.role == DRAIN) {
        jc["H"] = round1(k.H);
        jc["Hmax"] = round1(k.Hmax);
    } else {
        jc["d0"] = round1(k.d0);
        jc["t_yellow"] = k.tYellow;
        jc["t_red"] = k.tRed;
        jc["t_red_low"] = k.tRedLow;
        jc["t_yellow_low"] = k.tYellowLow;
        jc["stable_s"] = k.stableMs / 1000;
    }
    jc["period_s"] = k.periodMs / 1000.0f;
    doc["ts"] = epochNow();
    publishJson(c, c.tInfo, doc, true);
}

static void publishSample(Channel& c) {
    Sample s;
    portENTER_CRITICAL(&mux);
    if (!c.newSample) { portEXIT_CRITICAL(&mux); return; }
    s = c.sample;
    c.newSample = false;
    portEXIT_CRITICAL(&mux);

    JsonDocument doc;
    doc["node"] = c.id;
    doc["d"] = round1(s.d);
    if (c.role == DRAIN) {
        doc["m"] = round1(s.value);
        doc["L"] = round1(s.fill);
    } else {
        doc["h"] = round1(s.value);
        doc["muc"] = LEVEL_STR[s.level];
        doc["stable"] = s.stable;
        doc["buzzer_muted"] = s.muted;
    }
    doc["ts"] = epochNow();
    publishJson(c, c.tLevel, doc);
    Serial.printf("[PUB] %s d=%.1f value=%.1f\n", c.tLevel.c_str(), s.d, s.value);
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT — nhận cấu hình & lệnh
// ═════════════════════════════════════════════════════════════════════════════
static bool readNum(JsonObject o, const char* k, float& out) {
    if (!o[k].is<float>()) return false;
    out = o[k].as<float>();
    return true;
}

static void applyConfig(Channel& c, JsonObject o) {
    Config k = getCfg(c);
    float v;
    if (readNum(o, "H", v))            k.H = v;
    if (readNum(o, "Hmax", v))         k.Hmax = v;
    if (readNum(o, "d0", v))           k.d0 = v;
    if (readNum(o, "t_yellow", v))     k.tYellow = v;
    if (readNum(o, "t_red", v))        k.tRed = v;
    if (readNum(o, "t_red_low", v))    k.tRedLow = v;
    if (readNum(o, "t_yellow_low", v)) k.tYellowLow = v;
    if (readNum(o, "period_s", v))     k.periodMs = (uint32_t)(v * 1000.0f);
    if (readNum(o, "stable_s", v))     k.stableMs = (uint32_t)(v * 1000.0f);

    const char* err = nullptr;
    if (k.H < MIN_RANGE_CM || k.H > MAX_RANGE_CM)              err = "H ngoai khoang 20-450 cm";
    else if (k.Hmax <= 0 || k.Hmax > k.H)                      err = "Hmax phai trong (0, H]";
    else if (k.d0 < MIN_RANGE_CM || k.d0 > MAX_RANGE_CM)       err = "d0 ngoai khoang 20-450 cm";
    else if (!(k.tYellow > 0 && k.tYellow < k.tRed))           err = "can 0 < t_yellow < t_red";
    else if (!(k.tRedLow >= k.tYellow && k.tRedLow < k.tRed))  err = "can t_yellow <= t_red_low < t_red";
    else if (!(k.tYellowLow >= 0 && k.tYellowLow < k.tYellow)) err = "can 0 <= t_yellow_low < t_yellow";
    else if (k.periodMs < 1000 || k.periodMs > 60000)          err = "period_s trong 1-60";
    else if (k.stableMs > 300000)                              err = "stable_s toi da 300";

    if (err) {
        Serial.printf("[CFG] %s từ chối: %s\n", c.id, err);
        publishEvent(c, "config_rejected", nullptr, NAN, err);
        return;
    }
    setCfg(c, k);
    saveConfig(c, k);
    Serial.printf("[CFG] %s đã áp dụng cấu hình mới\n", c.id);
    publishEvent(c, "config_applied");
    publishInfo(c);
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
    String t(topic), msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim();
    Serial.printf("[SUB] %s → %s\n", topic, msg.c_str());

    for (uint8_t i = 0; i < N_CH; i++) {
        Channel& c = ch[i];
        if (c.role == ROAD && t == c.tAlarm) {
            if (msg == "off" && c.level == RED) c.muted = true;
            else if (msg == "on")               c.muted = false;
            c.buzzerEvent = true;
            return;
        }
        if (t == c.tConfig) {
            if (len == 0) return;                      // retained bị xoá
            JsonDocument doc;
            if (deserializeJson(doc, msg) || !doc.is<JsonObject>()) {
                publishEvent(c, "config_rejected", nullptr, NAN, "JSON khong hop le");
                return;
            }
            applyConfig(c, doc.as<JsonObject>());
            return;
        }
        if (t == c.tCmd) {
            String cmd = msg;
            JsonDocument doc;
            if (!deserializeJson(doc, msg) && doc["cmd"].is<const char*>()) cmd = doc["cmd"].as<const char*>();
            if (cmd == "calibrate")    c.calibrateRequest = true;
            else if (cmd == "report")  publishInfo(c);
            else if (cmd == "reboot") { publishEvent(c, "rebooting"); c.mqtt.loop(); delay(200); ESP.restart(); }
            else publishEvent(c, "unknown_cmd", nullptr, NAN, cmd.c_str());
            return;
        }
    }
}

static void mqttConnect(Channel& c) {
    String clientId = String("esp32-") + c.id + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    bool ok = (strlen(MQTT_USER) > 0)
        ? c.mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, c.tStatus.c_str(), 1, true, "offline")
        : c.mqtt.connect(clientId.c_str(), c.tStatus.c_str(), 1, true, "offline");
    if (!ok) { Serial.printf("[MQTT] %s kết nối thất bại, rc=%d\n", c.id, c.mqtt.state()); return; }
    Serial.printf("[MQTT] %s đã kết nối broker\n", c.id);
    c.mqtt.publish(c.tStatus.c_str(), "online", true);
    c.mqtt.subscribe(c.tConfig.c_str(), 1);
    c.mqtt.subscribe(c.tCmd.c_str(), 1);
    if (c.role == ROAD) c.mqtt.subscribe(c.tAlarm.c_str(), 1);
    publishInfo(c);
    c.lastInfo = millis();
}

// Không chặn phần đo: mất WiFi/broker chỉ làm hàm này return sớm.
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
    for (uint8_t i = 0; i < N_CH; i++) {
        Channel& c = ch[i];
        if (!c.mqtt.connected()) {
            if (now - c.lastMqttTry >= MQTT_RETRY_MS) { c.lastMqttTry = now; mqttConnect(c); }
            continue;
        }
        c.mqtt.loop();
    }
}

static void initChannel(Channel& c, const char* id, Role role, uint8_t trig, uint8_t echo) {
    c.id = id; c.role = role; c.trig = trig; c.echo = echo;
    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT);
    String base = String("flood/") + id + "/";
    c.tLevel = base + "level";   c.tStatus = base + "status"; c.tInfo = base + "info";
    c.tEvent = base + "event";   c.tConfig = base + "config"; c.tCmd = base + "cmd";
    c.tAlarm = base + "alarm/cmd";
    loadConfig(c);
    c.mqtt.setClient(c.net);
    c.mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    c.mqtt.setCallback(mqttCallback);
    c.mqtt.setBufferSize(768);
    c.mqtt.setSocketTimeout(3);
    if (role == ROAD) roadCh = &c;
}

// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== Flood node fw %s, %u kênh ===\n", FW_VERSION, N_CH);

#if NODE_MODE == MODE_DUAL
    initChannel(ch[0], DRAIN_ID, DRAIN, SENSOR1_TRIG, SENSOR1_ECHO);
    initChannel(ch[1], ROAD_ID,  ROAD,  SENSOR2_TRIG, SENSOR2_ECHO);
#elif NODE_MODE == MODE_DRAIN
    initChannel(ch[0], DRAIN_ID, DRAIN, SENSOR1_TRIG, SENSOR1_ECHO);
#else
    initChannel(ch[0], ROAD_ID,  ROAD,  SENSOR1_TRIG, SENSOR1_ECHO);
#endif

    if (roadCh) {
        pinMode(LED_GREEN_PIN, OUTPUT);
        pinMode(LED_YELLOW_PIN, OUTPUT);
        pinMode(LED_RED_PIN, OUTPUT);
        pinMode(BUZZER_PIN, OUTPUT);
        buzzerWrite(false);
        setLeds(GREEN);
    }
    // Task đo + cảnh báo tại chỗ: ưu tiên cao hơn loop(), không phụ thuộc mạng.
    xTaskCreatePinnedToCore(sensorTask, "sensor", 6144, nullptr, 2, nullptr, 1);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWifiTry = millis();
}

void loop() {
    networkLoop();
    for (uint8_t i = 0; i < N_CH; i++) {
        Channel& c = ch[i];
        publishSample(c);

        if (c.calibrateResult != 0) {
            if (c.calibrateResult > 0) {
                saveConfig(c, getCfg(c));
                publishEvent(c, "calibrated", c.role == DRAIN ? "H" : "d0", c.calibrateValue);
                publishInfo(c);
            } else {
                publishEvent(c, "calibrate_failed", nullptr, NAN, "khong do duoc khoang cach");
            }
            c.calibrateResult = 0;
        }

        if (c.buzzerEvent) {
            c.buzzerEvent = false;
            JsonDocument doc;
            doc["node"] = c.id;
            doc["event"] = "buzzer";
            doc["muted"] = (bool)c.muted;
            doc["ts"] = epochNow();
            publishJson(c, c.tEvent, doc);
        }

        if (c.mqtt.connected() && millis() - c.lastInfo >= INFO_INTERVAL_MS) {
            c.lastInfo = millis();
            publishInfo(c);
        }
    }
    delay(10);
}
