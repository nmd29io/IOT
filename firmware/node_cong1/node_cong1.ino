/**
 * node_cong1.ino  — ESP32 Firmware: Pipe Water-Level Node (cong1)
 *
 * Sensor   : JSN-SR04T ultrasonic (waterproof)
 * TRIG     : GPIO 5
 * ECHO     : GPIO 18
 *
 * Measurements:
 *   d        = raw distance from sensor to water surface (cm)
 *   level_cm = H_PIPE - d          (water level from pipe bottom)
 *   fill_pct = level_cm / H_MAX * 100
 *
 * Alert hysteresis state machine:
 *   GREEN  → YELLOW  when fill_pct >= 60 %
 *   YELLOW → GREEN   when fill_pct <  50 %
 *   YELLOW → RED     when fill_pct >= 80 %
 *   RED    → YELLOW  when fill_pct <  70 %
 *   Any state → RED  when rise_rate > 5 cm/min
 *
 * MQTT published topics:
 *   flood/cong1/level   {"level_cm":X,"fill_pct":Y,"rise_rate":Z,"status":"green|yellow|red","timestamp":T}
 *   flood/cong1/status  "green" | "yellow" | "red"
 *   flood/cong1/online  "online" (retained)   Last Will: "offline"
 *
 * Libraries required (install via Arduino Library Manager):
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "../../config.h"   // adjust path if config.h is copied locally

// ─── Topic strings ────────────────────────────────────────────────────────────
static const char* TOPIC_LEVEL   = "flood/cong1/level";
static const char* TOPIC_STATUS  = "flood/cong1/status";
static const char* TOPIC_ONLINE  = "flood/cong1/online";
static const char* CLIENT_ID     = "esp32_cong1";

// ─── Alert state ─────────────────────────────────────────────────────────────
enum AlertState { GREEN = 0, YELLOW = 1, RED = 2 };
static AlertState alertState = GREEN;

// ─── Moving-average buffer ────────────────────────────────────────────────────
static float   avgBuf[MOVING_AVG_SIZE] = {0};
static uint8_t avgIdx   = 0;
static bool    avgReady = false;

// ─── Rise-rate tracking ───────────────────────────────────────────────────────
static float   prevLevel    = 0.0f;
static uint32_t prevTime    = 0;
static float   riseRate     = 0.0f;   // cm / min

// ─── Timing ───────────────────────────────────────────────────────────────────
static uint32_t lastPublish = 0;
static uint32_t lastReconnectAttempt = 0;

// ─── WiFi / MQTT clients ──────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ═════════════════════════════════════════════════════════════════════════════
//  Median filter  —  takes N readings 50 ms apart, returns the middle value
// ═════════════════════════════════════════════════════════════════════════════
float medianFilter(uint8_t trigPin, uint8_t echoPin, uint8_t n = MEDIAN_SAMPLES) {
    float readings[MEDIAN_SAMPLES];

    for (uint8_t i = 0; i < n; i++) {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);

        long duration = pulseIn(echoPin, HIGH, 30000UL); // 30 ms timeout
        // If timeout → treat as max range (no echo)
        readings[i] = (duration == 0) ? 400.0f : (duration * 0.0343f / 2.0f);
        delay(MEDIAN_DELAY_MS);
    }

    // Bubble sort
    for (uint8_t i = 0; i < n - 1; i++)
        for (uint8_t j = 0; j < n - i - 1; j++)
            if (readings[j] > readings[j + 1]) {
                float tmp    = readings[j];
                readings[j]  = readings[j + 1];
                readings[j + 1] = tmp;
            }

    return readings[n / 2];
}

// ═════════════════════════════════════════════════════════════════════════════
//  Moving average
// ═════════════════════════════════════════════════════════════════════════════
float movingAverage(float newVal) {
    avgBuf[avgIdx] = newVal;
    avgIdx = (avgIdx + 1) % MOVING_AVG_SIZE;
    if (avgIdx == 0) avgReady = true;

    uint8_t count = avgReady ? MOVING_AVG_SIZE : avgIdx;
    float   sum   = 0;
    for (uint8_t i = 0; i < count; i++) sum += avgBuf[i];
    return sum / count;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Hysteresis alert state machine for cong1
// ═════════════════════════════════════════════════════════════════════════════
AlertState updateAlertState(float fillPct, float rate) {
    // Rate override → RED immediately
    if (rate > 5.0f) {
        alertState = RED;
        return alertState;
    }

    switch (alertState) {
        case GREEN:
            if (fillPct >= 60.0f) alertState = YELLOW;
            break;
        case YELLOW:
            if (fillPct <  50.0f) alertState = GREEN;
            else if (fillPct >= 80.0f) alertState = RED;
            break;
        case RED:
            if (fillPct < 70.0f) alertState = YELLOW;
            break;
    }
    return alertState;
}

// ═════════════════════════════════════════════════════════════════════════════
//  WiFi setup
// ═════════════════════════════════════════════════════════════════════════════
void setupWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] FAILED — will retry in loop");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT connect / reconnect
// ═════════════════════════════════════════════════════════════════════════════
bool mqttConnect() {
    // Last Will (retained)
    if (mqttClient.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                           TOPIC_ONLINE, 1, true, "offline")) {
        Serial.println("[MQTT] Connected");
        // Announce online (retained)
        mqttClient.publish(TOPIC_ONLINE, "online", true);
        return true;
    }
    Serial.printf("[MQTT] Connect failed, rc=%d\n", mqttClient.state());
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
//  setup()
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Flood Monitor — cong1 ===");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    setupWiFi();

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setBufferSize(512);
    mqttConnect();

    prevTime  = millis();
    lastPublish = millis();
}

// ═════════════════════════════════════════════════════════════════════════════
//  loop()
// ═════════════════════════════════════════════════════════════════════════════
void loop() {
    // ── WiFi watchdog ──────────────────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Disconnected — reconnecting…");
        WiFi.reconnect();
        delay(5000);
        return;
    }

    // ── MQTT watchdog ──────────────────────────────────────────────────────
    if (!mqttClient.connected()) {
        uint32_t now = millis();
        if (now - lastReconnectAttempt > MQTT_RECONNECT_MS) {
            lastReconnectAttempt = now;
            mqttConnect();
        }
    }
    mqttClient.loop();

    // ── Sensor read & publish ──────────────────────────────────────────────
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();

        // 1. Median-filtered raw distance
        float d = medianFilter(TRIG_PIN, ECHO_PIN);

        // 2. Clamp to valid range
        d = constrain(d, 0.0f, H_PIPE);

        // 3. Water level & fill
        float levelCm = H_PIPE - d;
        levelCm       = constrain(levelCm, 0.0f, H_PIPE);
        float fillPct = (levelCm / H_MAX) * 100.0f;
        fillPct       = constrain(fillPct, 0.0f, 100.0f);

        // 4. Moving average on level
        float smoothLevel = movingAverage(levelCm);

        // 5. Rise rate (cm / min)
        uint32_t now = millis();
        float dt     = (now - prevTime) / 60000.0f;  // minutes
        if (dt > 0.0f) {
            riseRate = (smoothLevel - prevLevel) / dt;
        }
        prevLevel = smoothLevel;
        prevTime  = now;
        // Clamp absurd values (sensor noise on first reading)
        if (riseRate > 100.0f || riseRate < -100.0f) riseRate = 0.0f;

        // 6. Alert state
        AlertState state = updateAlertState(fillPct, riseRate);
        const char* statusStr = (state == RED) ? "red" :
                                (state == YELLOW) ? "yellow" : "green";

        // 7. Build JSON payload
        StaticJsonDocument<256> doc;
        doc["level_cm"]  = round(smoothLevel * 10) / 10.0;
        doc["fill_pct"]  = round(fillPct * 10) / 10.0;
        doc["rise_rate"] = round(riseRate * 10) / 10.0;
        doc["status"]    = statusStr;
        doc["timestamp"] = now;

        char buf[256];
        serializeJson(doc, buf);

        // 8. Publish
        if (mqttClient.connected()) {
            mqttClient.publish(TOPIC_LEVEL,  buf);
            mqttClient.publish(TOPIC_STATUS, statusStr);
            Serial.printf("[PUB] %s → %s\n", TOPIC_LEVEL, buf);
        } else {
            Serial.println("[MQTT] Not connected — skipping publish");
        }
    }
}
