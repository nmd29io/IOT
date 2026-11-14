"""
Test Simulator — Thử Nghiệm Từng Node Cảm Biến & Kịch Bản Ngập Lụt
"""
import json
import time
import paho.mqtt.client as mqtt

print("=" * 65)
print("   KIỂM THỬ CÁC NODE CẢM BIẾN VÀ KỊCH BẢN NGẬP LỤT TRÊN MQTT")
print("=" * 65)

try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="test_simulator_runner")
except AttributeError:
    client = mqtt.Client(client_id="test_simulator_runner")

client.connect("localhost", 1883, 60)

# 1. Báo online
print("\n[BƯỚC 1] Phát tín hiệu Online cho 2 node:")
client.publish("flood/cong1/online", "online", qos=1, retain=True)
client.publish("flood/duong1/online", "online", qos=1, retain=True)
print(" -> Node 1 (Cống Thái Hà): ONLINE")
print(" -> Node 2 (Mặt đường Thái Hà): ONLINE")
time.sleep(1)

# Các kịch bản thử nghiệm
scenarios = [
    {
        "name": "KỊCH BẢN 1: AN TOÀN (Trời nắng ráo, cống cạn)",
        "cong1": {"level_cm": 42.0, "fill_pct": 23.3, "rise_rate": 0.0, "status": "green"},
        "duong1": {"depth_cm": 0.0, "status": "green"},
        "radar": {"rain_current": 0.0, "rain_15m": 0.0, "rain_30m": 0.0, "rain_60m": 0.0, "downstream_river": 38.0},
        "desc": "Mực nước thấp, đèn xanh, giao thông lưu thông bình thường."
    },
    {
        "name": "KỊCH BẢN 2: CẢNH BÁO (Bắt đầu có mưa dông, nước cống dâng nhanh)",
        "cong1": {"level_cm": 105.0, "fill_pct": 58.3, "rise_rate": 2.5, "status": "yellow"},
        "duong1": {"depth_cm": 4.5, "status": "yellow"},
        "radar": {"rain_current": 0.85, "rain_15m": 10.0, "rain_30m": 22.0, "rain_60m": 35.0, "downstream_river": 55.0},
        "desc": "Mực nước cống vượt 50%, đèn chuyển Vàng, cảnh báo theo dõi."
    },
    {
        "name": "KỊCH BẢN 3: NGUY HIỂM / KHẨN CẤP (Mưa cực lớn, cống quá tải tràn ra đường)",
        "cong1": {"level_cm": 162.0, "fill_pct": 90.0, "rise_rate": 4.8, "status": "red"},
        "duong1": {"depth_cm": 19.5, "status": "red"},
        "radar": {"rain_current": 1.35, "rain_15m": 18.0, "rain_30m": 42.0, "rain_60m": 70.0, "downstream_river": 85.0},
        "desc": "Cống đầy 90%, mặt đường ngập 19.5cm -> Đèn Đỏ + Còi hú, AI báo nguy cơ ngập giao thông 30m!"
    },
    {
        "name": "KỊCH BẢN 4: HỒI PHỤC (Mưa ngớt, trạm bơm hoạt động, nước rút)",
        "cong1": {"level_cm": 65.0, "fill_pct": 36.1, "rise_rate": -1.8, "status": "green"},
        "duong1": {"depth_cm": 2.0, "status": "green"},
        "radar": {"rain_current": 0.1, "rain_15m": 1.0, "rain_30m": 2.0, "rain_60m": 4.0, "downstream_river": 46.0},
        "desc": "Nước rút về mức an toàn, đèn xanh trở lại nhờ cơ chế Hysteresis."
    }
]

for idx, sc in enumerate(scenarios, 1):
    print(f"\n--- [{idx}/4] {sc['name']} ---")
    print(f"Ý nghĩa: {sc['desc']}")
    
    now_ts = int(time.time())
    sc['cong1']['timestamp'] = now_ts
    sc['duong1']['timestamp'] = now_ts
    
    # Publish dữ liệu từ 2 node và Radar
    client.publish("flood/cong1/level", json.dumps(sc['cong1']), qos=1)
    client.publish("flood/duong1/depth", json.dumps(sc['duong1']), qos=1)
    client.publish("flood/weather/radar", json.dumps(sc['radar']), qos=1)
    
    print(f" -> [Node 1 Cống] : {sc['cong1']['level_cm']} cm ({sc['cong1']['fill_pct']}%)")
    print(f" -> [Node 2 Đường]: Ngập {sc['duong1']['depth_cm']} cm [Đèn: {sc['duong1']['status'].upper()}]")
    print(f" -> [Radar Mưa]  : Cường độ {sc['radar']['rain_current']} mm/phút | Mưa 30m: {sc['radar']['rain_30m']} mm | Sông: {sc['radar']['downstream_river']} cm")
    
    # Chờ 3 giây để Node-RED và Edge AI xử lý
    time.sleep(3)

client.disconnect()
print("\n" + "=" * 65)
print("   HOÀN TẤT KIỂM THỬ 4 KỊCH BẢN THỜI GIAN THỰC!")
print("   Kiểm tra giao diện Dashboard tại: http://localhost:1880/ui")
print("=" * 65)
