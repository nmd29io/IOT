"""
Bộ mô phỏng node ESP32 — kiểm thử hệ thống khi chưa cắm mạch thật.

    python test_simulator.py              # 4 kịch bản (an toàn → cảnh báo → khẩn cấp → hồi phục)
    python test_simulator.py --loop       # chu kỳ mưa lặp liên tục (demo dashboard, AI)
    python test_simulator.py --host 192.168.1.100

Mỗi node ảo dùng đúng giao thức của firmware v2 (firmware/flood_node):
  - flood/<id>/status  online/offline (Last Will)      - flood/<id>/info  heartbeat
  - flood/<id>/level   dữ liệu đo                      - flood/<id>/event sự kiện
  - nhận flood/<id>/config, flood/<id>/cmd (calibrate/report), flood/duong1/alarm/cmd (off/on)
Node đường ảo tự phân mức có hysteresis + thời gian ổn định giống firmware.
ĐÂY LÀ DỮ LIỆU MÔ PHỎNG — khi báo cáo cần ghi rõ.
"""
import argparse
import json
import random
import threading
import time

import paho.mqtt.client as mqtt

LEVELS = ["green", "yellow", "red"]


def make_client(client_id):
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:
        return mqtt.Client(client_id=client_id)


class VirtualNode:
    def __init__(self, node_id, role, host, port):
        self.id, self.role = node_id, role
        self.base = f"flood/{node_id}/"
        self.started = time.time()
        if role == "drain":
            self.cfg = {"H": 200.0, "Hmax": 180.0, "period_s": 2}
        else:
            self.cfg = {"d0": 40.0, "t_yellow": 10.0, "t_red": 20.0, "t_red_low": 15.0,
                        "t_yellow_low": 8.0, "stable_s": 20, "period_s": 2}
        self.value = 0.0          # m (cống) hoặc h (đường), cm
        self.level, self.pending, self.pending_since = "green", "green", time.time()
        self.muted = False
        self.lock = threading.Lock()

        self.c = make_client(f"sim-{node_id}")
        self.c.will_set(self.base + "status", "offline", qos=1, retain=True)
        self.c.on_connect = self._on_connect
        self.c.on_message = self._on_message
        self.c.connect(host, port, 30)
        self.c.loop_start()

    # ── MQTT ──────────────────────────────────────────────────────────────
    def _on_connect(self, client, userdata, flags, rc, properties=None):
        client.publish(self.base + "status", "online", qos=1, retain=True)
        client.subscribe([(self.base + "config", 1), (self.base + "cmd", 1), (self.base + "alarm/cmd", 1)])
        self.publish_info()

    def _event(self, event, **kw):
        self.c.publish(self.base + "event", json.dumps({"node": self.id, "event": event, **kw, "ts": int(time.time())}))

    def _on_message(self, client, userdata, msg):
        text = msg.payload.decode("utf-8", "ignore").strip()
        if msg.topic.endswith("alarm/cmd"):
            with self.lock:
                if text == "off" and self.level == "red":
                    self.muted = True
                elif text == "on":
                    self.muted = False
                self._event("buzzer", muted=self.muted)
            print(f"   [{self.id}] lệnh còi: {text}")
        elif msg.topic.endswith("/config"):
            if not text:
                return
            try:
                new = json.loads(text)
                keys = set(self.cfg)
                with self.lock:
                    self.cfg.update({k: float(v) for k, v in new.items() if k in keys})
                self._event("config_applied")
                self.publish_info()
                print(f"   [{self.id}] áp dụng cấu hình: {new}")
            except Exception as e:  # noqa: BLE001
                self._event("config_rejected", reason=str(e))
        elif msg.topic.endswith("/cmd"):
            try:
                cmd = json.loads(text).get("cmd", text)
            except Exception:  # noqa: BLE001
                cmd = text
            if cmd == "calibrate":
                with self.lock:
                    if self.role == "road":       # đường đang khô → d đo được chính là d0
                        self.cfg["d0"] = round(self.cfg["d0"] - self.value + random.uniform(-0.3, 0.3), 1)
                        key, val = "d0", self.cfg["d0"]
                    else:                          # cống rỗng → d đo được chính là H
                        self.cfg["H"] = round(self.cfg["H"] - self.value + random.uniform(-0.3, 0.3), 1)
                        key, val = "H", self.cfg["H"]
                self._event("calibrated", key=key, value=val)
                self.publish_info()
                print(f"   [{self.id}] hiệu chuẩn: {key} = {val}")
            elif cmd == "report":
                self.publish_info()

    def publish_info(self):
        info = {"node": self.id, "role": self.role, "fw": "sim-2.0.0", "ip": "simulator",
                "rssi": random.randint(-70, -45), "uptime_s": int(time.time() - self.started),
                "heap": 210000, "sensor_err": 0, "cfg": self.cfg, "ts": int(time.time())}
        self.c.publish(self.base + "info", json.dumps(info), qos=1, retain=True)

    # ── Đo & phân mức ─────────────────────────────────────────────────────
    def _classify(self, h):
        c = self.cfg
        target = 2 if h > c["t_red"] else 1 if h >= c["t_yellow"] else 0
        cur = LEVELS.index(self.level)
        if target < cur:
            if cur == 2 and h >= c["t_red_low"]:
                target = 2
            elif target == 0 and h >= c["t_yellow_low"]:
                target = 1
        return LEVELS[target]

    def publish_sample(self, value):
        with self.lock:
            self.value = max(0.0, value)
            noise = random.uniform(-0.2, 0.2)
            if self.role == "drain":
                m = min(self.value + noise, self.cfg["H"])
                L = max(0.0, min(100.0, m / self.cfg["Hmax"] * 100))
                payload = {"node": self.id, "d": round(self.cfg["H"] - m, 1), "m": round(m, 1),
                           "L": round(L, 1), "ts": int(time.time())}
            else:
                h = max(0.0, self.value + noise)
                cand, now = self._classify(h), time.time()
                if cand == self.level:
                    self.pending = self.level
                elif cand != self.pending:
                    self.pending, self.pending_since = cand, now
                elif now - self.pending_since >= self.cfg["stable_s"]:
                    self.level = cand
                    if cand != "red" and self.muted:
                        self.muted = False
                payload = {"node": self.id, "d": round(self.cfg["d0"] - h, 1), "h": round(h, 1),
                           "muc": self.level, "stable": self.pending == self.level,
                           "buzzer_muted": self.muted, "ts": int(time.time())}
        self.c.publish(self.base + "level", json.dumps(payload), qos=1)
        return payload

    def stop(self):
        self.c.publish(self.base + "status", "offline", qos=1, retain=True).wait_for_publish(2)
        self.c.loop_stop()
        self.c.disconnect()


def publish_radar(client, rain_now, rain_30m, river):
    client.publish("flood/weather/radar", json.dumps({
        "rain_current": rain_now, "rain_15m": round(rain_30m * 0.4, 1), "rain_30m": rain_30m,
        "rain_60m": round(rain_30m * 1.6, 1), "downstream_river": river}), qos=1)


SCENARIOS = [
    ("KỊCH BẢN 1: AN TOÀN – trời nắng, cống cạn", 40.0, 0.0, (0.0, 0.0, 38.0)),
    ("KỊCH BẢN 2: CẢNH BÁO – mưa dông, cống > 50%", 100.0, 4.0, (0.85, 22.0, 55.0)),
    ("KỊCH BẢN 3: KHẨN CẤP – cống > 80%, đường ngập > 20 cm", 160.0, 24.0, (1.35, 42.0, 85.0)),
    ("KỊCH BẢN 4: HỒI PHỤC – mưa ngớt, bơm hoạt động, nước rút", 60.0, 0.0, (0.1, 2.0, 46.0)),
]


def run_scenarios(cong, duong, radar_client, seconds):
    for i, (name, m, h, radar) in enumerate(SCENARIOS, 1):
        print(f"\n--- [{i}/{len(SCENARIOS)}] {name} ---")
        start = time.time()
        m0 = cong.value
        while time.time() - start < seconds:
            frac = min(1.0, (time.time() - start) / (seconds * 0.4))   # dâng/rút dần trong 40% thời gian
            pc = cong.publish_sample(m0 + (m - m0) * frac)
            pd = duong.publish_sample(h)
            publish_radar(radar_client, *radar)
            print(f"   cống {pc['m']:6.1f} cm ({pc['L']:5.1f}%) | đường {pd['h']:5.1f} cm [{pd['muc']}]", end="\r")
            time.sleep(2)
        print()


def run_loop(cong, duong, radar_client):
    t, m, h = 0, 40.0, 0.0
    while True:
        t += 1
        rain, rain30, river = 0.1, 3.0, 42.0
        if t <= 15:                       # bình thường
            m, rain, rain30 = 40.0 + t * 1.0, 0.2, 5.0
        elif t <= 45:                     # mưa to, nước cống dâng nhanh (~ 4,5 cm / 2 s)
            m, rain, rain30, river = min(165.0, m + 4.5), 0.95, 25.0, 68.0
        elif t <= 80:                     # tràn ra đường
            m = 165.0 + random.uniform(-2, 2)
            h = min(25.0, h + 1.0)
            rain, rain30, river = 1.1, 35.0, 78.0
        else:                             # rút dần
            m, h = max(40.0, m - 3.0), max(0.0, h - 0.8)
            rain, rain30, river = 0.05, 1.0, 45.0
            if m <= 40.0 and h <= 0.0:
                t = 0
        pc, pd = cong.publish_sample(m), duong.publish_sample(h)
        if t % 2 == 0:
            publish_radar(radar_client, rain, rain30, river)
        print(f"[t={t:3d}] cống {pc['m']:6.1f} cm ({pc['L']:5.1f}%) | đường {pd['h']:5.1f} cm [{pd['muc']}]")
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser(description="Mô phỏng node cong1 / duong1")
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--loop", action="store_true", help="chạy chu kỳ mưa liên tục")
    ap.add_argument("--seconds", type=int, default=30, help="thời lượng mỗi kịch bản (s)")
    args = ap.parse_args()

    print("=" * 65)
    print("   MÔ PHỎNG NODE cong1 + duong1 (giao thức firmware v2)")
    print("   Ctrl+C để dừng – node sẽ báo offline")
    print("=" * 65)
    cong = VirtualNode("cong1", "drain", args.host, args.port)
    duong = VirtualNode("duong1", "road", args.host, args.port)
    radar = make_client("sim-radar")
    radar.connect(args.host, args.port, 30)
    radar.loop_start()
    time.sleep(1)
    try:
        if args.loop:
            run_loop(cong, duong, radar)
        else:
            run_scenarios(cong, duong, radar, args.seconds)
            print("\nHoàn tất 4 kịch bản. Dashboard: http://localhost:1880/ui")
    except KeyboardInterrupt:
        print("\nĐang dừng…")
    finally:
        cong.stop()
        duong.stop()
        radar.loop_stop()


if __name__ == "__main__":
    main()
