/**
 * node_duong1.ino  — ESP32 Firmware: Road Flood-Depth Node (duong1)
 *
 * Sensor   : JSN-SR04T ultrasonic (waterproof)
 * TRIG     : GPIO 5
 * ECHO     : GPIO 18
 *
 * Actuators:
 *   LED GREEN  : GPIO 26
 *   LED YELLOW : GPIO 27
 *   LED RED    : GPIO 14
 *   BUZZER     : GPIO 12
 *
 * Measurements:
 *   d        = raw distance from sensor to water/road surface (cm)
 *   depth_cm = D0_ROAD - d   (flood depth; 0 when road is dry)
 *
 * Alert hysteresis state machine (duong1):
 *   GREEN  → YELLOW  when depth_cm >= 5  cm
 *   YELLOW → GREEN   when depth_cm <  3  cm
 *   YELLOW → RED     when depth_cm >= 15 cm
 *   RED    → YELLOW  when depth_cm <  10 cm
 *
 * Stability filter: value accepted only if readings stay within ±1 cm
 * for at least STABLE_WINDOW_MS (20 s).
 *
 * MQTT published topics:
 *   flood/duong1/depth   {"depth_cm":X,"status":"green|yellow|red","timestamp":T}
 *   flood/duong1/online  "online" (retained)   Last Will: "offline"
 *
 * MQTT subscribed topics:
 *   flood/duong1/buzzer  → payload "off" silences the buzzer
 *
 * Libraries required:
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "../../config.h"   // adjust path if config.h is copied locally

// ─── Actuator pins ────────────────────────────────────────────────────────────
#define LED_GREEN   26
#define LED_YELLOW  27
#define LED_RED     14
#define BUZZER_PIN  12

// ─── MQTT Topics ──────────────────────────────────────────────────────────────
static const char* TOPIC_DEPTH   = "flood/duong1/depth";
static const char* TOPIC_ONLINE  = "flood/duong1/online";
static const char* TOPIC_BUZZER  = "flood/duong1/buzzer";
static const char* CLIENT_ID     = "esp32_duong1";

// ─── Alert state ─────────────────────────────────────────────────────────────
enum AlertState { GREEN = 0, YELLOW = 1, RED = 2 };
static AlertState alertState = GREEN;

// ─── Stability filter state ───────────────────────────────────────────────────
#define STABLE_WINDOW_MS  20000UL   // 20 seconds
#define STABLE_TOLERANCE  1.0f      // ± cm

static float    stableRef        = -1.0f;   // reference reading
static uint32_t stableStart      = 0;
static bool     stableAccepted   = false;
static float    acceptedDepth    = 0.0f;

// ─── Moving-average buffer ────────────────────────────────────────────────────
static float   avgBuf[MOVING_AVG_SIZE] = {0};
static uint8_t avgIdx   = 0;
static bool    avgReady = false;

// ─── Buzzer mute flag ─────────────────────────────────────────────────────────
static volatile bool buzzerMuted = false;

// ─── Timing ───────────────────────────────────────────────────────────────────
static uint32_t lastPublish          = 0;
static uint32_t lastReconnectAttempt = 0;

// ─── WiFi / MQTT ─────────────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ═════════════════════════════════════════════════════════════════════════════
//  Median filter
// ═════════════════════════════════════════════════════════════════════════════
float medianFilter(uint8_t trigPin, uint8_t echoPin, uint8_t n = MEDIAN_SAMPLES) {
    float readings[MEDIAN_SAMPLES];

    for (uint8_t i = 0; i < n; i++) {
        digitalWrite(trigPin, LOW);
        delayMicroseconds(2);
        digitalWrite(trigPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(trigPin, LOW);

        long duration = pulseIn(echoPin, HIGH, 30000UL);
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
//  Stability filter
//  Returns true and updates acceptedDepth when the reading has been stable
//  (within STABLE_TOLERANCE) for STABLE_WINDOW_MS milliseconds.
// ═════════════════════════════════════════════════════════════════════════════
bool stabilityFilter(float rawDepth) {
    uint32_t now = millis();

    if (stableRef < 0.0f) {
        // First call — initialise reference
        stableRef   = rawDepth;
        stableStart = now;
        return stableAccepted;
    }

    if (fabsf(rawDepth - stableRef) > STABLE_TOLERANCE) {
        // New value — restart timer
        stableRef        = rawDepth;
        stableStart      = now;
        stableAccepted   = false;
    } else if (!stableAccepted && (now - stableStart >= STABLE_WINDOW_MS)) {
        // Stable long enough — accept
        acceptedDepth  = rawDepth;
        stableAccepted = true;
    }

    return stableAccepted;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Hysteresis alert state machine for duong1
// ═════════════════════════════════════════════════════════════════════════════
AlertState updateAlertState(float depth) {
    switch (alertState) {
        case GREEN:
            if (depth >= 5.0f) alertState = YELLOW;
            break;
        case YELLOW:
            if      (depth <  3.0f)  alertState = GREEN;
            else if (depth >= 15.0f) alertState = RED;
            break;
        case RED:
            if (depth < 10.0f) alertState = YELLOW;
            break;
    }
    return alertState;
}

// ═════════════════════════════════════════════════════════════════════════════
//  LED + Buzzer control
// ═════════════════════════════════════════════════════════════════════════════
void setActuators(AlertState state) {
    // Turn all LEDs off first
    digitalWrite(LED_GREEN,  LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED,    LOW);

    switch (state) {
        case GREEN:
            digitalWrite(LED_GREEN, HIGH);
            noTone(BUZZER_PIN);
            buzzerMuted = false;   // reset mute when safe
            break;
        case YELLOW:
            digitalWrite(LED_YELLOW, HIGH);
            if (!buzzerMuted) tone(BUZZER_PIN, 1000);  // 1 kHz tone
            break;
        case RED:
            digitalWrite(LED_RED, HIGH);
            if (!buzzerMuted) tone(BUZZER_PIN, 2500);  // higher pitch
            break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT message callback
// ═════════════════════════════════════════════════════════════════════════════
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char msg[64] = {0};
    if (length < sizeof(msg)) {
        memcpy(msg, payload, length);
    }
    Serial.printf("[SUB] %s → %s\n", topic, msg);

    if (strcmp(topic, TOPIC_BUZZER) == 0 && strcmp(msg, "off") == 0) {
        buzzerMuted = true;
        noTone(BUZZER_PIN);
        Serial.println("[ACT] Buzzer silenced via MQTT");
    }
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
    if (mqttClient.connect(CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                           TOPIC_ONLINE, 1, true, "offline")) {
        Serial.println("[MQTT] Connected");
        mqttClient.publish(TOPIC_ONLINE, "online", true);
        mqttClient.subscribe(TOPIC_BUZZER);
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
    Serial.println("\n=== Flood Monitor — duong1 ===");

    // Sensor
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    // Actuators
    pinMode(LED_GREEN,  OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_RED,    OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // Safe start state
    setActuators(GREEN);

    setupWiFi();

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    mqttConnect();

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

        // 2. Flood depth (negative means sensor is too close — clamp to 0)
        float rawDepth = D0_ROAD - d;
        if (rawDepth < 0.0f) rawDepth = 0.0f;

        // 3. Moving average
        float smoothDepth = movingAverage(rawDepth);

        // 4. Stability filter — only update alert with accepted value
        bool stable = stabilityFilter(smoothDepth);
        float reportDepth = stable ? acceptedDepth : acceptedDepth; // keep last accepted

        // 5. Alert state (using stable or last accepted depth)
        AlertState state = updateAlertState(reportDepth);
        const char* statusStr = (state == RED) ? "red" :
                                (state == YELLOW) ? "yellow" : "green";

        // 6. Drive actuators
        setActuators(state);

        // 7. Build JSON payload
        StaticJsonDocument<192> doc;
        doc["depth_cm"] = round(reportDepth * 10) / 10.0;
        doc["status"]   = statusStr;
        doc["stable"]   = stable;
        doc["timestamp"]= millis();

        char buf[192];
        serializeJson(doc, buf);

        // 8. Publish
        if (mqttClient.connected()) {
            mqttClient.publish(TOPIC_DEPTH, buf);
            Serial.printf("[PUB] %s → %s\n", TOPIC_DEPTH, buf);
        } else {
            Serial.println("[MQTT] Not connected — skipping publish");
        }
    }
}
