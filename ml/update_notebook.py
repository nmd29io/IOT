import json
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
nb_path = BASE_DIR / "IOT.ipynb"

notebook = {
    "cells": [
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "# 🌊 HỆ THỐNG GIÁM SÁT & CẢNH BÁO NGẬP ÚNG ĐÔ THỊ TỰ ĐỘNG\n",
                "## Khối AI/ML: Dự Báo Đa Tầng Thời Gian Tích Hợp Thủy Lực Manning & Địa Hình GIS\n",
                "**Học phần:** Phát triển ứng dụng Internet of Things (INT3323_1) — Trường Đại học Công nghệ, ĐHQGHN\n",
                "**Giảng viên:** TS. Nguyễn Ngọc Tân\n",
                "**Nhóm 1:** Nguyễn Thị Vân Anh (23021468) | Nguyễn Minh Đức (23020049) | Nguyễn Đăng Huy (23021573)\n",
                "\n",
                "---\n",
                "### 🎯 Đột Phá Kỹ Thuật Của Khối AI/ML (Physics & GIS-Informed ML):\n",
                "Thay vì chỉ dựa vào mực nước đơn thuần, mô hình được tích hợp các **nguyên lý Thủy lực & Địa hình GIS chuyên ngành**:\n",
                "1. **Địa hình & Dòng chảy mặt GIS (Rational Method):** $Q_{inflow} = \\frac{C \\cdot I \\cdot A}{360}$\n",
                "   - $A$: Diện tích lưu vực thu nước tuyến phố (ví dụ lưu vực Thái Hà $12.5\\text{ ha}$).\n",
                "   - $C$: Hệ số dòng chảy bề mặt bê tông hóa ($0.85$ cho đô thị nén).\n",
                "   - $I$: Cường độ mưa tức thời và dự báo từ Radar Nowcasting ($\\text{mm/h}$).\n",
                "2. **Năng lực thoát cống theo Thủy lực Manning:** $Q_{\\max} = \\frac{1}{n} A R^{2/3} S^{1/2}$\n",
                "   - Tính toán lưu lượng thoát tối đa qua khẩu độ cống bê tông ($D = 1000\\text{ mm}$, $n=0.013$, độ dốc $0.2\\%$).\n",
                "3. **Hệ số quá tải thủy lực (Hydraulic Overload Ratio):** $Ratio = \\frac{Q_{inflow}}{Q_{capacity}}$\n",
                "   - Khi $Ratio > 1.0$: Lượng nước đổ về vượt quá năng lực thiết kế cống $\\to$ Chắc chắn gây ngập úng!\n",
                "4. **Hiệu ứng nước dội ngược từ sông xả (Backwater Effect):**\n",
                "   - Khi mực nước sông Tô Lịch/sông Nhuệ dâng cao, cản trở cống tự chảy, giảm hiệu suất xả đến $70\\%$."
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### 1. Khởi Tạo Môi Trường & Đọc Cấu Hình Trạm Đo (GIS & Hydraulic Config)"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "import os\n",
                "import json\n",
                "from pathlib import Path\n",
                "import numpy as np\n",
                "import pandas as pd\n",
                "import matplotlib.pyplot as plt\n",
                "from sklearn.ensemble import RandomForestRegressor\n",
                "from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score\n",
                "import joblib\n",
                "\n",
                "try:\n",
                "    BASE_DIR = Path(__file__).resolve().parent\n",
                "except NameError:\n",
                "    BASE_DIR = Path('.').resolve()\n",
                "\n",
                "OUT_DIR = BASE_DIR / 'ml' / 'models'\n",
                "OUT_DIR.mkdir(parents=True, exist_ok=True)\n",
                "\n",
                "# Đọc cấu hình GIS & Thủy lực\n",
                "cfg_path = BASE_DIR / 'config' / 'gis_hydraulic_params.json'\n",
                "with open(cfg_path, 'r', encoding='utf-8') as f:\n",
                "    cfg = json.load(f)\n",
                "\n",
                "print(f\"Trạm đo           : {cfg['station_name']}\")\n",
                "print(f\"Lưu vực gom nước  : {cfg['gis_catchment']['catchment_area_ha']} ha | Hệ số mặt đệm C: {cfg['gis_catchment']['runoff_coefficient_C']}\")\n",
                "print(f\"Khẩu độ cống ngầm : {cfg['pipe_hydraulics']['pipe_diameter_mm']} mm | Năng lực tải Manning: {cfg['pipe_hydraulics']['max_discharge_capacity_m3s']} m³/s\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### 2. Sinh Dữ Liệu Thủy Văn Đô Thị Tích Hợp Công Thức Rational & Manning\n",
                "Tạo tập dữ liệu 6,000 phút với 30 đợt mưa rào kết hợp với quá tải thủy lực và nước dội ngược từ sông Tô Lịch."
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "rng = np.random.default_rng(2026)\n",
                "n = 6000\n",
                "t = np.arange(n)\n",
                "\n",
                "C_runoff = cfg['gis_catchment']['runoff_coefficient_C']\n",
                "A_ha = cfg['gis_catchment']['catchment_area_ha']\n",
                "Q_max_pipe = cfg['pipe_hydraulics']['max_discharge_capacity_m3s']\n",
                "\n",
                "# Chuỗi mưa thực tế & dự báo Nowcasting\n",
                "rain = np.zeros(n)\n",
                "for _ in range(30):\n",
                "    start = rng.integers(0, n - 200)\n",
                "    dur = rng.integers(30, 180)\n",
                "    peak = rng.uniform(0.4, 1.4)\n",
                "    end = min(n, start + dur)\n",
                "    rain[start:end] += np.sin(np.linspace(0, np.pi, end - start)) * peak\n",
                "\n",
                "rain_fc_15m = np.clip([np.sum(rain[i:min(n, i+15)]) + rng.normal(0, 0.05) for i in range(n)], 0, None)\n",
                "rain_fc_30m = np.clip([np.sum(rain[i:min(n, i+30)]) + rng.normal(0, 0.1) for i in range(n)], 0, None)\n",
                "rain_fc_60m = np.clip([np.sum(rain[i:min(n, i+60)]) + rng.normal(0, 0.2) for i in range(n)], 0, None)\n",
                "\n",
                "# Mực nước sông Tô Lịch (35cm - 100cm)\n",
                "river = np.zeros(n)\n",
                "for i in range(1, n):\n",
                "    river[i] = np.clip(river[i-1] + 0.16 * rain[i] - 0.04 + rng.normal(0, 0.02), 35.0, 100.0)\n",
                "\n",
                "# 1. Lưu lượng gom nước theo Rational Method: Q = (C * I * A) / 360 (m3/s)\n",
                "inflow_m3s = (C_runoff * (rain * 60.0) * A_ha) / 360.0\n",
                "\n",
                "# 2. Hệ số dội ngược từ sông Tô Lịch & Năng lực tải Manning thực tế\n",
                "backwater_ratio = np.clip((river - 35.0) / 65.0, 0.0, 1.0)\n",
                "actual_cap_m3s = Q_max_pipe * (1.0 - 0.70 * backwater_ratio)\n",
                "\n",
                "# 3. Hệ số quá tải thủy lực (Overload Ratio)\n",
                "overload_ratio = np.round(inflow_m3s / np.maximum(0.1, actual_cap_m3s), 2)\n",
                "\n",
                "# Mô phỏng mực nước cống\n",
                "water = np.zeros(n)\n",
                "for i in range(1, n):\n",
                "    net_flow = inflow_m3s[i] - actual_cap_m3s[i] * (water[i-1] / 180.0)\n",
                "    water[i] = np.clip(water[i-1] + net_flow * 25.0 + rng.normal(0, 0.03), 0.0, 180.0)\n",
                "\n",
                "df = pd.DataFrame({\n",
                "    'water_level_cm': np.round(water, 1),\n",
                "    'fill_pct': np.round((water / 180.0) * 100, 1),\n",
                "    'rate_1m': np.round(pd.Series(water).diff(1).fillna(0), 2),\n",
                "    'rate_3m': np.round(((pd.Series(water) - pd.Series(water).shift(3)) / 3).fillna(0), 2),\n",
                "    'rate_5m': np.round(((pd.Series(water) - pd.Series(water).shift(5)) / 5).fillna(0), 2),\n",
                "    'accel_1m': np.round(pd.Series(water).diff(1).diff(1).fillna(0), 2),\n",
                "    'rain_current': np.round(rain, 3),\n",
                "    'rain_forecast_15m': np.round(rain_fc_15m, 2),\n",
                "    'rain_forecast_30m': np.round(rain_fc_30m, 2),\n",
                "    'rain_forecast_60m': np.round(rain_fc_60m, 2),\n",
                "    'catchment_inflow_m3s': np.round(inflow_m3s, 3),\n",
                "    'manning_actual_cap_m3s': np.round(actual_cap_m3s, 3),\n",
                "    'hydraulic_overload_ratio': overload_ratio,\n",
                "    'backwater_ratio': np.round(backwater_ratio, 3),\n",
                "    'downstream_river_level': np.round(river, 1)\n",
                "})\n",
                "\n",
                "df['target_5m_cm'] = df['water_level_cm'].shift(-5)\n",
                "df['target_15m_cm'] = df['water_level_cm'].shift(-15)\n",
                "df['target_30m_cm'] = df['water_level_cm'].shift(-30)\n",
                "df['target_60m_cm'] = df['water_level_cm'].shift(-60)\n",
                "df = df.dropna().reset_index(drop=True)\n",
                "print(f\"Đã tạo {df.shape[0]} mẫu dữ liệu tích hợp thủy lực & GIS.\")"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### 3. Huấn Luyện & Đánh Giá Đa Chân Trời Thời Gian (5m - 15m - 30m - 60m)"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "features = [\n",
                "    'water_level_cm', 'fill_pct', 'rate_1m', 'rate_3m', 'rate_5m', 'accel_1m',\n",
                "    'rain_current', 'rain_forecast_15m', 'rain_forecast_30m', 'rain_forecast_60m',\n",
                "    'catchment_inflow_m3s', 'manning_actual_cap_m3s', 'hydraulic_overload_ratio',\n",
                "    'backwater_ratio', 'downstream_river_level'\n",
                "]\n",
                "\n",
                "X = df[features]\n",
                "split = int(len(df) * 0.8)\n",
                "X_train, X_test = X.iloc[:split], X.iloc[split:]\n",
                "\n",
                "horizons = [5, 15, 30, 60]\n",
                "models = {}\n",
                "results = []\n",
                "importances = {}\n",
                "\n",
                "for h in horizons:\n",
                "    y_tr = df[f'target_{h}m_cm'].iloc[:split]\n",
                "    y_te = df[f'target_{h}m_cm'].iloc[split:]\n",
                "    \n",
                "    rf = RandomForestRegressor(n_estimators=100, max_depth=12, min_samples_leaf=2, random_state=42, n_jobs=1)\n",
                "    rf.fit(X_train, y_tr)\n",
                "    pred = rf.predict(X_test)\n",
                "    \n",
                "    mae = mean_absolute_error(y_te, pred)\n",
                "    rmse = np.sqrt(mean_squared_error(y_te, pred))\n",
                "    r2 = r2_score(y_te, pred)\n",
                "    \n",
                "    models[h] = rf\n",
                "    importances[h] = rf.feature_importances_\n",
                "    results.append({'Mốc dự báo': f'+{h} phút', 'MAE (cm)': round(mae, 3), 'RMSE (cm)': round(rmse, 3), 'R2 Score': round(r2, 4)})\n",
                "    joblib.dump(rf, OUT_DIR / f'flood_model_{h}m.joblib')\n",
                "\n",
                "res_df = pd.DataFrame(results)\n",
                "print(res_df.to_string(index=False))"
            ]
        },
        {
            "cell_type": "markdown",
            "metadata": {},
            "source": [
                "### 4. Phân Tích Độ Quan Trọng Của Thông Số Thủy Lực & Khí Tượng"
            ]
        },
        {
            "cell_type": "code",
            "execution_count": None,
            "metadata": {},
            "outputs": [],
            "source": [
                "df_shift = pd.DataFrame({\n",
                "    '+5m (%)': (pd.Series(importances[5], index=features) * 100).round(2),\n",
                "    '+60m (%)': (pd.Series(importances[60], index=features) * 100).round(2)\n",
                "}).sort_values(by='+60m (%)', ascending=False)\n",
                "\n",
                "print('BẢNG SO SÁNH TRỌNG SỐ ĐẶC TRƯNG:')\n",
                "print(df_shift.to_string())\n",
                "\n",
                "# Trực quan hóa\n",
                "fig, axes = plt.subplots(1, 2, figsize=(15, 5))\n",
                "pd.Series(importances[5], index=features).sort_values().iloc[-6:].plot(kind='barh', ax=axes[0], color='#2980b9')\n",
                "axes[0].set_title('Mốc +5m: Hệ Số Quá Tải Thủy Lực Chi Phối', fontweight='bold')\n",
                "axes[0].set_xlabel('Importance Score')\n",
                "\n",
                "pd.Series(importances[60], index=features).sort_values().iloc[-6:].plot(kind='barh', ax=axes[1], color='#d35400')\n",
                "axes[1].set_title('Mốc +60m: Mưa Radar 60m & Lưu Lượng GIS Chi Phối', fontweight='bold')\n",
                "axes[1].set_xlabel('Importance Score')\n",
                "plt.tight_layout()\n",
                "plt.show()"
            ]
        }
    ],
    "metadata": {
        "language_info": {
            "name": "python",
            "version": "3.13.0"
        }
    },
    "nbformat": 4,
    "nbformat_minor": 4
}

with open(nb_path, "w", encoding="utf-8") as f:
    json.dump(notebook, f, indent=2, ensure_ascii=False)

print(f"Cập nhật thành công {nb_path} tích hợp đầy đủ Thủy Lực & Địa Hình GIS!")
