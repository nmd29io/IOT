"""
Edge ML Real-Time Service — Dự Báo Ngập Úng Tích Hợp Thủy Lực Manning & Địa Hình GIS
Đề tài: Hệ thống Giám sát & Cảnh báo Ngập úng Đô thị Tự động - UET/VNU
Chạy trên tầng Stage 3 (Edge IT)

Nhiệm vụ:
1. Đọc file cấu hình GIS & Thủy lực (config/gis_hydraulic_params.json):
   - Diện tích lưu vực thu nước A (ha), Hệ số dòng chảy C.
   - Khẩu độ cống, năng lực tải tối đa Manning Q_max (m3/s).
2. Tự động tính toán các biến vật lý theo thời gian thực:
   - Lưu lượng nước đổ về: Q_inflow = (C * I * A) / 360.
   - Hệ số quá tải cống: Overload_ratio = Q_inflow / Q_capacity.
   - Hệ số cản trở dòng chảy do sông hạ lưu dâng (Backwater effect).
3. Chạy 4 mô hình Random Forest cho 4 mốc thời gian (+5m, +15m, +30m, +60m).
4. Publish kết quả lên MQTT topic 'flood/ai/prediction' và ghi log CSV.
"""

import json
import time
from pathlib import Path
from collections import deque
import pandas as pd
import numpy as np
import joblib
import paho.mqtt.client as mqtt

BASE_DIR = Path(__file__).resolve().parent.parent
MODEL_DIR = BASE_DIR / "ml" / "models"
DATA_DIR = BASE_DIR / "data"
CONFIG_FILE = BASE_DIR / "config" / "gis_hydraulic_params.json"
LOG_CSV_PATH = DATA_DIR / "sensor_history.csv"

# 1. NẠP CẤU HÌNH GIS & THỦY LỰC
if CONFIG_FILE.exists():
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        station_cfg = json.load(f)
else:
    station_cfg = {
        "gis_catchment": {"catchment_area_ha": 12.5, "runoff_coefficient_C": 0.85},
        "pipe_hydraulics": {"max_discharge_capacity_m3s": 1.45}
    }

C_runoff = station_cfg["gis_catchment"]["runoff_coefficient_C"]
A_ha = station_cfg["gis_catchment"]["catchment_area_ha"]
Q_max_pipe = station_cfg["pipe_hydraulics"]["max_discharge_capacity_m3s"]

print("=" * 70)
print(f"   DỊCH VỤ AI MULTI-HORIZON TÍCH HỢP THỦY LỰC MANNING & ĐỊA HÌNH GIS")
print(f"   - Trạm đo: {station_cfg.get('station_name', 'Cống 1')}")
print(f"   - Lưu vực GIS: {A_ha} ha (Hệ số thấm C = {C_runoff})")
print(f"   - Năng lực tải Manning: {Q_max_pipe} m3/s")
print("=" * 70)

# Nạp 4 mô hình
models = {}
for h in [5, 15, 30, 60]:
    p = MODEL_DIR / f"flood_model_{h}m.joblib"
    if p.exists():
        models[h] = joblib.load(p)
        print(f" - Đã nạp mô hình +{h}m: {p.name}")

history_cong1 = deque(maxlen=60)
current_road_depth = 0.0

if not LOG_CSV_PATH.exists():
    with open(LOG_CSV_PATH, "w", encoding="utf-8") as f:
        f.write("timestamp,level_cm,fill_pct,rate_1m,inflow_m3s,overload_ratio,pred_5m,pred_15m,pred_30m,pred_60m,prob_30m,status_5m,status_30m\n")

def get_rate(history, delta_seconds):
    if len(history) < 2:
        return 0.0
    now_t, now_val = history[-1]
    target_t = now_t - delta_seconds
    closest = min(history, key=lambda x: abs(x[0] - target_t))
    dt_sec = now_t - closest[0]
    if dt_sec < 5.0:
        return 0.0
    dt_min = dt_sec / 60.0
    raw_rate = (now_val - closest[1]) / dt_min
    return max(-25.0, min(25.0, raw_rate))

radar_forecast = {
    "rain_current": 0.0,
    "rain_15m": 0.0,
    "rain_30m": 0.0,
    "rain_60m": 0.0,
    "downstream_river": 45.0
}

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("[MQTT] Đã kết nối thành công tới Mosquitto Broker!")
        client.subscribe([
            ("flood/cong1/level", 1),
            ("flood/duong1/level", 1),
            ("flood/weather/radar", 1)
        ])
        print("[MQTT] Đã subscribe: flood/cong1/level, flood/duong1/level, flood/weather/radar")
    else:
        print(f"[MQTT ERROR] Mã lỗi kết nối: {rc}")

def on_message(client, userdata, msg):
    global current_road_depth, radar_forecast
    now = time.time()

    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except Exception:
        return

    if msg.topic == "flood/duong1/level":
        current_road_depth = float(payload.get("h", payload.get("depth_cm", 0.0)))
        return

    if msg.topic == "flood/weather/radar":
        radar_forecast.update(payload)
        return

    if msg.topic == "flood/cong1/level":
        # Firmware v2 gửi {"m": mực nước cm, "L": % đầy}; vẫn đọc được định dạng cũ level_cm/fill_pct
        level_cm = float(payload.get("m", payload.get("level_cm", 0.0)))
        level_cm = max(0.0, min(180.0, level_cm))
        fill_pct = float(payload.get("L", payload.get("fill_pct", (level_cm / 180.0) * 100)))
        fill_pct = max(0.0, min(100.0, fill_pct))

        history_cong1.append((now, level_cm))

        rate_1m = get_rate(history_cong1, 60)
        rate_3m = get_rate(history_cong1, 180)
        rate_5m = get_rate(history_cong1, 300)

        # Ưu tiên tốc độ dâng tính từ Node-RED / ESP32 nếu có
        if payload.get("rise_rate") is not None:
            try:
                firmware_rate = float(payload.get("rise_rate"))
                if abs(firmware_rate) <= 25.0:
                    rate_1m = firmware_rate
            except Exception:
                pass

        rate_30s_prev = get_rate(list(history_cong1)[:-1], 30) if len(history_cong1) > 2 else 0.0
        accel_1m = max(-10.0, min(10.0, rate_1m - rate_30s_prev))

        # 1. Tính toán lưu lượng và thông số thủy lực thời gian thực
        # Chỉ ước tính mưa đổ về khi nước đang dâng thực tế
        est_rain = max(0.0, min(15.0, rate_1m * 0.5)) if rate_1m > 0 else 0.0
        inflow_m3s = (C_runoff * (est_rain * 60.0) * A_ha) / 360.0

        river_lvl = radar_forecast.get("downstream_river", 45.0)
        backwater_ratio = min(1.0, max(0.0, (river_lvl - 35.0) / 65.0))
        actual_cap_m3s = Q_max_pipe * (1.0 - 0.70 * backwater_ratio)
        overload_ratio = round(inflow_m3s / max(0.1, actual_cap_m3s), 2)

        rain_15 = max(radar_forecast.get("rain_15m", 0.0), est_rain * 15 * 0.8)
        rain_30 = max(radar_forecast.get("rain_30m", 0.0), est_rain * 30 * 0.85)
        rain_60 = max(radar_forecast.get("rain_60m", 0.0), est_rain * 60 * 0.9)

        features = [
            "water_level_cm", "fill_pct", "rate_1m", "rate_3m", "rate_5m", "accel_1m",
            "rain_current", "rain_forecast_15m", "rain_forecast_30m", "rain_forecast_60m",
            "catchment_inflow_m3s", "manning_actual_cap_m3s", "hydraulic_overload_ratio",
            "backwater_ratio", "downstream_river_level"
        ]

        input_data = pd.DataFrame([{
            "water_level_cm": level_cm,
            "fill_pct": fill_pct,
            "rate_1m": rate_1m,
            "rate_3m": rate_3m,
            "rate_5m": rate_5m,
            "accel_1m": accel_1m,
            "rain_current": est_rain,
            "rain_forecast_15m": rain_15,
            "rain_forecast_30m": rain_30,
            "rain_forecast_60m": rain_60,
            "catchment_inflow_m3s": round(inflow_m3s, 3),
            "manning_actual_cap_m3s": round(actual_cap_m3s, 3),
            "hydraulic_overload_ratio": overload_ratio,
            "backwater_ratio": round(backwater_ratio, 3),
            "downstream_river_level": river_lvl
        }])[features]

        # Dự báo 4 mốc
        pred_5m = float(models[5].predict(input_data)[0]) if 5 in models else level_cm
        pred_15m = float(models[15].predict(input_data)[0]) if 15 in models else level_cm
        pred_30m = float(models[30].predict(input_data)[0]) if 30 in models else level_cm
        pred_60m = float(models[60].predict(input_data)[0]) if 60 in models else level_cm

        pred_5m = max(0.0, min(180.0, pred_5m))
        pred_15m = max(0.0, min(180.0, pred_15m))
        pred_30m = max(0.0, min(180.0, pred_30m))
        pred_60m = max(0.0, min(180.0, pred_60m))

        fill_5m = round((pred_5m / 180.0) * 100, 1)
        fill_30m = round((pred_30m / 180.0) * 100, 1)

        # Xác suất ngập sau 30m (%) dựa trên độ đầy và hệ số quá tải Manning
        base_prob = (pred_30m / 144.0) * 75.0
        if overload_ratio > 1.0:
            base_prob += (overload_ratio - 1.0) * 15.0
        prob_flood_30m = min(99.0, max(5.0, round(base_prob, 1)))

        # Trạng thái 5m (Tại chỗ)
        if fill_5m >= 80.0 or overload_ratio >= 1.5:
            status_5m = "NGUY HIỂM"
            color_5m = "#e74c3c"
        elif fill_5m >= 50.0 or overload_ratio >= 1.0:
            status_5m = "CẢNH BÁO"
            color_5m = "#f39c12"
        else:
            status_5m = "AN TOÀN"
            color_5m = "#2ecc71"

        # Trạng thái 30m (Giao thông)
        if prob_flood_30m >= 75.0 or pred_30m >= 144.0:
            status_30m = "NGUY CƠ NGẬP CAO"
            traffic_advisory = f"🚨 CẢNH BÁO GIAO THÔNG: Lưu vực {A_ha}ha quá tải thủy lực ({overload_ratio}x). Dự báo ngập sau 30 phút!"
            color_30m = "#e74c3c"
            early_warning = True
        elif prob_flood_30m >= 50.0:
            status_30m = "NGUY CƠ TRUNG BÌNH"
            traffic_advisory = f"⚠️ THEO DÕI: Cống đang hoạt động ở mức {round(overload_ratio*100)}% công suất Manning."
            color_30m = "#f39c12"
            early_warning = False
        else:
            status_30m = "AN TOÀN THOÁT NƯỚC"
            traffic_advisory = "Lưu lượng thoát ổn định trong giới hạn thiết kế Manning."
            color_30m = "#2ecc71"
            early_warning = False

        result = {
            "current_level": round(level_cm, 1),
            "current_fill": round(fill_pct, 1),
            "rate_1m": round(rate_1m, 2),
            "inflow_m3s": round(inflow_m3s, 2),
            "hydraulic_overload_ratio": overload_ratio,
            "predicted_level_5m": round(pred_5m, 1),
            "predicted_fill_5m": fill_5m,
            "predicted_level_15m": round(pred_15m, 1),
            "predicted_level_30m": round(pred_30m, 1),
            "predicted_level_60m": round(pred_60m, 1),
            "predicted_status": status_5m,
            "status_color": color_5m,
            "prob_flood_30m": prob_flood_30m,
            "status_30m": status_30m,
            "status_30m_color": color_30m,
            "traffic_advisory": traffic_advisory,
            "early_warning": early_warning,
            "warning_msg": f"Dự báo +5m: {pred_5m:.1f}cm | +30m: {pred_30m:.1f}cm (Quá tải: {overload_ratio}x)",
            "timestamp": int(now)
        }

        client.publish("flood/ai/prediction", json.dumps(result, ensure_ascii=False), qos=1)

        print(f"🌊 [GIS & HYDRAULIC AI] Hiện tại: {level_cm:.1f}cm | "
              f"Lưu lượng vào: {inflow_m3s:.2f}m³/s (Quá tải: {overload_ratio}x) | "
              f"+5m: {pred_5m:.1f}cm [{status_5m}] | "
              f"+30m: {pred_30m:.1f}cm (Nguy cơ: {prob_flood_30m}%) | +60m: {pred_60m:.1f}cm", flush=True)

        try:
            with open(LOG_CSV_PATH, "a", encoding="utf-8") as f:
                f.write(f"{int(now)},{level_cm:.2f},{fill_pct:.1f},{rate_1m:.2f},{inflow_m3s:.2f},{overload_ratio},"
                        f"{pred_5m:.2f},{pred_15m:.2f},{pred_30m:.2f},{pred_60m:.2f},{prob_flood_30m:.1f},{status_5m},{status_30m}\n")
        except Exception:
            pass

def main():
    print("=" * 70)
    print("   DỊCH VỤ AI THỜI GIAN THỰC TÍCH HỢP THỦY LỰC & GIS")
    print("   Lắng nghe MQTT và chạy dự báo đa tầng...")
    print("=" * 70)

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="gis_hydraulic_ai_predictor")
    except AttributeError:
        client = mqtt.Client(client_id="gis_hydraulic_ai_predictor")

    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect("localhost", 1883, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[INFO] Đang dừng dịch vụ AI...")
        client.disconnect()
    except Exception as e:
        print(f"[ERROR] Lỗi kết nối MQTT: {e}")

if __name__ == "__main__":
    main()
