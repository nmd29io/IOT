"""
Mô hình AI/ML Dự Báo Mực Nước Tích Hợp Thông Số Thủy Lực (Manning) & Địa Hình GIS (Rational Method)
Đề tài: Hệ thống Giám sát & Cảnh báo Ngập úng Đô thị Tự động - UET/VNU

Tích hợp đầy đủ các đặc trưng vật lý & thủy văn:
1. Địa hình GIS (Lưu vực gom nước, hệ số dòng chảy mặt C, diện tích lưu vực A).
2. Thủy lực mạng cống (Công thức Manning, lưu lượng xả tối đa, hệ số quá tải thủy lực).
3. Hiệu ứng nước dội ngược từ sông xả hạ lưu (Backwater Head Ratio).
4. Dự báo mưa Nowcasting đa mốc thời gian (+5m, +15m, +30m, +60m).
"""

import os
import json
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
os.environ["MPLCONFIGDIR"] = str(BASE_DIR / "ml" / ".mpl")

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from sklearn.ensemble import RandomForestRegressor
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import joblib

MODEL_DIR = BASE_DIR / "ml" / "models"
FIGURE_DIR = BASE_DIR / "ml" / "figures"
DATA_DIR = BASE_DIR / "data"
CONFIG_FILE = BASE_DIR / "config" / "gis_hydraulic_params.json"

MODEL_DIR.mkdir(parents=True, exist_ok=True)
FIGURE_DIR.mkdir(parents=True, exist_ok=True)
DATA_DIR.mkdir(parents=True, exist_ok=True)

# 1. NẠP THÔNG SỐ ĐỊA HÌNH GIS VÀ THỦY LỰC TỪ CẤU HÌNH
if CONFIG_FILE.exists():
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        station_cfg = json.load(f)
else:
    station_cfg = {
        "gis_catchment": {"catchment_area_ha": 12.5, "runoff_coefficient_C": 0.85, "surface_slope_pct": 0.35},
        "pipe_hydraulics": {"pipe_diameter_mm": 1000, "pipe_slope_i": 0.002, "manning_roughness_n": 0.013, "max_discharge_capacity_m3s": 1.45}
    }

C_runoff = station_cfg["gis_catchment"]["runoff_coefficient_C"] # Hệ số dòng chảy C = 0.85
A_ha = station_cfg["gis_catchment"]["catchment_area_ha"]         # Lưu vực A = 12.5 ha
Q_max_pipe = station_cfg["pipe_hydraulics"]["max_discharge_capacity_m3s"] # Năng lực tải Manning Q_max = 1.45 m3/s

print("=" * 75)
print("   HUẤN LUYỆN MÔ HÌNH AI TÍCH HỢP THỦY LỰC MANNING & ĐỊA HÌNH GIS")
print(f"   [GIS] Diện tích lưu vực: {A_ha} ha | Hệ số mặt đệm C: {C_runoff}")
print(f"   [Thủy lực] Khẩu độ cống: {station_cfg['pipe_hydraulics']['pipe_diameter_mm']} mm | Năng lực tải Manning Qmax: {Q_max_pipe} m3/s")
print("=" * 75)

# 2. SINH DỮ LIỆU THỦY VĂN ĐÔ THỊ TÍCH HỢP CÔNG THỨC RATIONAL & MANNING
rng = np.random.default_rng(2026)
n = 6000
t = np.arange(n)

# Tạo chuỗi mưa
rain_series = np.zeros(n)
for _ in range(30):
    start = rng.integers(0, n - 200)
    dur = rng.integers(30, 180)
    peak = rng.uniform(0.4, 1.4)
    end = min(n, start + dur)
    rain_series[start:end] += np.sin(np.linspace(0, np.pi, end - start)) * peak

# Lượng mưa dự báo Nowcasting 15m, 30m, 60m (mm tích lũy)
rain_fc_15m = np.zeros(n)
rain_fc_30m = np.zeros(n)
rain_fc_60m = np.zeros(n)
for i in range(n):
    rain_fc_15m[i] = np.sum(rain_series[i:min(n, i+15)]) + rng.normal(0, 0.05)
    rain_fc_30m[i] = np.sum(rain_series[i:min(n, i+30)]) + rng.normal(0, 0.1)
    rain_fc_60m[i] = np.sum(rain_series[i:min(n, i+60)]) + rng.normal(0, 0.2)

rain_fc_15m = np.clip(rain_fc_15m, 0, None)
rain_fc_30m = np.clip(rain_fc_30m, 0, None)
rain_fc_60m = np.clip(rain_fc_60m, 0, None)

# Mực nước sông tiếp nhận hạ lưu (Sông Tô Lịch: 35cm - 100cm)
downstream_river = np.zeros(n)
for i in range(1, n):
    downstream_river[i] = np.clip(
        downstream_river[i-1] + 0.16 * rain_series[i] - 0.04 + rng.normal(0, 0.02),
        35.0, 100.0
    )

# Tính toán các đại lượng Thủy lực & GIS theo công thức ngành:
# a) Lưu lượng dòng chảy mặt đổ về hố ga theo công thức Rational Method: Q = (C * I * A) / 360
# I ở đây tính ra mm/giờ (rain_series * 60)
inflow_m3s = (C_runoff * (rain_series * 60.0) * A_ha) / 360.0

# b) Tỷ số dội áp ngược từ sông hạ lưu (Backwater Ratio)
# Khi mực nước sông > 60cm, năng lực thoát cống giảm mạnh
backwater_ratio = np.clip((downstream_river - 35.0) / (100.0 - 35.0), 0.0, 1.0)

# c) Năng lực tải thực tế sau khi bị sông cản trở (m3/s)
actual_drain_cap_m3s = Q_max_pipe * (1.0 - 0.70 * backwater_ratio)

# d) Hệ số quá tải thủy lực (Overload Ratio = Inflow / Capacity)
# > 1.0 nghĩa là nước vào cống nhanh hơn nước thoát -> cống dâng và tràn!
overload_ratio = np.round(inflow_m3s / np.maximum(0.1, actual_drain_cap_m3s), 2)

# Mô phỏng mực nước cống từ chênh lệch lưu lượng (m3/s -> cm nước trong cống)
water = np.zeros(n)
pipe_volume_factor = 25.0 # Hệ số quy đổi dung tích lưu cống sang chiều cao cm
for i in range(1, n):
    net_flow = inflow_m3s[i] - actual_drain_cap_m3s[i] * (water[i-1] / 180.0)
    delta_h = net_flow * pipe_volume_factor + rng.normal(0, 0.03)
    water[i] = np.clip(water[i-1] + delta_h, 0.0, 180.0)

# 3. ĐÓNG GÓI BẢNG ĐẶC TRƯNG TÍCH HỢP THỦY LỰC & GIS
df = pd.DataFrame({
    "water_level_cm": np.round(water, 1),
    "fill_pct": np.round((water / 180.0) * 100, 1),
    "rate_1m": np.round(pd.Series(water).diff(1).fillna(0), 2),
    "rate_3m": np.round(((pd.Series(water) - pd.Series(water).shift(3)) / 3).fillna(0), 2),
    "rate_5m": np.round(((pd.Series(water) - pd.Series(water).shift(5)) / 5).fillna(0), 2),
    "accel_1m": np.round(pd.Series(water).diff(1).diff(1).fillna(0), 2),
    # Nhóm Khí tượng Nowcasting
    "rain_current": np.round(rain_series, 3),
    "rain_forecast_15m": np.round(rain_fc_15m, 2),
    "rain_forecast_30m": np.round(rain_fc_30m, 2),
    "rain_forecast_60m": np.round(rain_fc_60m, 2),
    # Nhóm Địa hình GIS & Thủy lực Manning
    "catchment_inflow_m3s": np.round(inflow_m3s, 3),
    "manning_actual_cap_m3s": np.round(actual_drain_cap_m3s, 3),
    "hydraulic_overload_ratio": overload_ratio,
    "backwater_ratio": np.round(backwater_ratio, 3),
    "downstream_river_level": np.round(downstream_river, 1)
})

# Mục tiêu dự báo
df["target_5m_cm"] = df["water_level_cm"].shift(-5)
df["target_15m_cm"] = df["water_level_cm"].shift(-15)
df["target_30m_cm"] = df["water_level_cm"].shift(-30)
df["target_60m_cm"] = df["water_level_cm"].shift(-60)

df = df.dropna().reset_index(drop=True)
df.to_csv(DATA_DIR / "flood_gis_hydraulic_data.csv", index=False)
print(f"[OK] Đã xuất dataset Thủy Lực & GIS: {len(df)} mẫu -> data/flood_gis_hydraulic_data.csv")

# 4. HUẤN LUYỆN ĐA MÔ HÌNH VỚI ĐẶC TRƯNG THỦY LỰC
features = [
    "water_level_cm", "fill_pct", "rate_1m", "rate_3m", "rate_5m", "accel_1m",
    "rain_current", "rain_forecast_15m", "rain_forecast_30m", "rain_forecast_60m",
    "catchment_inflow_m3s", "manning_actual_cap_m3s", "hydraulic_overload_ratio",
    "backwater_ratio", "downstream_river_level"
]

X = df[features]
split = int(len(df) * 0.8)
X_train, X_test = X.iloc[:split], X.iloc[split:]

horizons = [5, 15, 30, 60]
eval_results = []
feature_importances_dict = {}

print("\n--- KẾT QUẢ ĐÁNH GIÁ MÔ HÌNH TÍCH HỢP THỦY LỰC (5m, 15m, 30m, 60m) ---")
for h in horizons:
    y_train = df[f"target_{h}m_cm"].iloc[:split]
    y_test = df[f"target_{h}m_cm"].iloc[split:]

    rf = RandomForestRegressor(n_estimators=100, max_depth=12, min_samples_leaf=2, random_state=42, n_jobs=1)
    rf.fit(X_train, y_train)
    pred = rf.predict(X_test)

    mae = mean_absolute_error(y_test, pred)
    rmse = np.sqrt(mean_squared_error(y_test, pred))
    r2 = r2_score(y_test, pred)

    eval_results.append({
        "horizon": f"+{h} phút",
        "MAE (cm)": round(mae, 3),
        "RMSE (cm)": round(rmse, 3),
        "R2 Score": round(r2, 4)
    })
    feature_importances_dict[h] = rf.feature_importances_
    joblib.dump(rf, MODEL_DIR / f"flood_model_{h}m.joblib")
    print(f"Mốc +{h:2d} phút: MAE = {mae:6.3f} cm | RMSE = {rmse:6.3f} cm | R2 = {r2:.4f}")

eval_df = pd.DataFrame(eval_results)
print("\n" + eval_df.to_string(index=False))

# 5. XUẤT BIỂU ĐỒ SO SÁNH & FEATURE IMPORTANCE MỚI
fig, axes = plt.subplots(1, 2, figsize=(15, 6))

# Biểu đồ 1: Top 7 đặc trưng mốc 5 phút
imp_5m = pd.Series(feature_importances_dict[5], index=features).sort_values(ascending=True)[-7:]
imp_5m.plot(kind='barh', ax=axes[0], color='#2980b9')
axes[0].set_title('Mốc +5 Phút (Chốt chặn tại chỗ)\nMực nước hiện tại & Quán tính chi phối', fontsize=11, fontweight='bold')
axes[0].set_xlabel('Importance Score')

# Biểu đồ 2: Top 7 đặc trưng mốc 60 phút
imp_60m = pd.Series(feature_importances_dict[60], index=features).sort_values(ascending=True)[-7:]
imp_60m.plot(kind='barh', ax=axes[1], color='#d35400')
axes[1].set_title('Mốc +60 Phút (Giao thông & Hồ điều hòa)\nMưa Nowcasting, Hệ số quá tải Manning & Sông hạ lưu', fontsize=11, fontweight='bold')
axes[1].set_xlabel('Importance Score')

plt.suptitle('Tác Động Của Địa Hình GIS & Thủy Lực Manning Lên Dự Báo Ngập Úng', fontsize=13, fontweight='bold')
plt.tight_layout()
fig_shift_path = FIGURE_DIR / "feature_importance_shift.png"
plt.savefig(fig_shift_path, dpi=300)
plt.close()
print(f"[OK] Đã xuất biểu đồ Feature Importance mới: {fig_shift_path}")

# Xuất thêm biểu đồ so sánh MAE
plt.figure(figsize=(9, 4.5))
plt.bar([f"+{h} phút" for h in horizons], [r["MAE (cm)"] for r in eval_results], color='#16a085', width=0.4)
for i, r in enumerate(eval_results):
    plt.text(i, r["MAE (cm)"] + 0.05, f"{r['MAE (cm)']:.2f} cm", ha='center', fontweight='bold')
plt.title('Sai Số Dự Báo Tuyệt Đối (MAE) Khi Tích Hợp Thông Số Thủy Lực & GIS', fontsize=12, fontweight='bold')
plt.ylabel('MAE (cm)')
plt.grid(True, alpha=0.3)
plt.tight_layout()
fig_comp_path = FIGURE_DIR / "multi_horizon_comparison.png"
plt.savefig(fig_comp_path, dpi=300)
plt.close()
print(f"[OK] Đã xuất biểu đồ so sánh MAE: {fig_comp_path}")

print("=" * 75)
print("   HOÀN TẤT HUẤN LUYỆN TOÀN BỘ MÔ HÌNH VỚI ĐẶC TRƯNG THỦY LỰC & GIS!")
print("=" * 75)
