import json
import shutil
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
flow_file = BASE_DIR / "nodered" / "flows.json"
user_flow_file = BASE_DIR / "nodered" / ".node-red" / "flows.json"

with open(flow_file, "r", encoding="utf-8") as f:
    flows = json.load(f)

# Lọc bỏ các node AI cũ nếu có để nạp phiên bản Multi-Horizon mới
ai_node_ids = {
    "ui_group_ai", "mqtt_in_ai_pred", "fn_process_ai", "ui_gauge_ai_5m",
    "ui_gauge_ai_30m", "ui_text_ai_status", "ui_text_ai_warning", "ui_chart_ai_compare",
    "ui_text_traffic_advisory"
}
flows = [n for n in flows if n.get("id") not in ai_node_ids]

# Cập nhật thứ tự các group hiện tại
for n in flows:
    if n.get("id") == "ui_group_alert":
        n["order"] = 4
    elif n.get("id") == "ui_group_chart":
        n["order"] = 5

ai_group = {
    "id": "ui_group_ai",
    "type": "ui_group",
    "name": "🤖 Dự Báo AI Đa Tầng Thời Gian (5m - 15m - 30m - 60m)",
    "tab": "ui_tab_flood",
    "order": 3,
    "disp": True,
    "width": "12",
    "collapse": False
}
flows.append(ai_group)

mqtt_in_ai = {
    "id": "mqtt_in_ai_pred",
    "type": "mqtt in",
    "z": "tab_flood_main",
    "name": "ai/prediction",
    "topic": "flood/ai/prediction",
    "qos": "1",
    "datatype": "json",
    "broker": "mqtt_broker_config",
    "x": 120,
    "y": 480,
    "wires": [["fn_process_ai"]]
}
flows.append(mqtt_in_ai)

js_code = """var p = msg.payload;
if (!p) return null;

// Output 1: Gauge dự báo mực nước 5m
var msgGauge5m = {
    payload: p.predicted_level_5m,
    topic: 'Dự báo +5m'
};

// Output 2: Gauge xác suất ngập 30m (%)
var msgGauge30m = {
    payload: p.prob_flood_30m,
    topic: 'Xác suất ngập +30m (%)'
};

// Output 3: Trạng thái đa tầng (Badge HTML)
var badge = '<div style="line-height:1.8;">' +
    '<b>Mốc +5m:</b> <span style="display:inline-block;padding:2px 8px;border-radius:8px;color:#fff;background:' + p.status_color + ';font-weight:bold;">' + p.predicted_status + ' (' + p.predicted_fill_5m + '%)</span><br>' +
    '<b>Mốc +30m:</b> <span style="display:inline-block;padding:2px 8px;border-radius:8px;color:#fff;background:' + p.status_30m_color + ';font-weight:bold;">' + p.status_30m + ' (' + p.prob_flood_30m + '%)</span><br>' +
    '<b>Mực nước:</b> Hiện tại: ' + p.current_level + 'cm | +15m: ' + p.predicted_level_15m + 'cm | +30m: ' + p.predicted_level_30m + 'cm | +60m: ' + p.predicted_level_60m + 'cm' +
    '</div>';
var msgStatus = {
    payload: badge
};

// Output 4: Khuyến nghị điều phối giao thông
var warnColor = p.early_warning ? '#e74c3c' : '#2ecc71';
var msgTraffic = {
    payload: '<div style="padding:8px;border-left:4px solid ' + warnColor + ';background:#f9f9f9;font-weight:600;color:' + warnColor + ';">' + p.traffic_advisory + '</div>'
};

// Output 5: Biểu đồ đa đường (Actual, +5m, +30m, +60m)
var msgChartActual = { topic: '1. Mực nước thực tế (cm)', payload: p.current_level };
var msgChart5m     = { topic: '2. Dự báo AI +5m (cm)',   payload: p.predicted_level_5m };
var msgChart30m    = { topic: '3. Dự báo AI +30m (cm)',  payload: p.predicted_level_30m };
var msgChart60m    = { topic: '4. Dự báo AI +60m (cm)',  payload: p.predicted_level_60m };

// Output 6: Telegram early warning alert
var msgTg = null;
if (p.early_warning) {
    var lastAlert = flow.get('last_ai_alert_time') || 0;
    var now = Date.now();
    if (now - lastAlert > 180000) { // Mỗi 3 phút
        flow.set('last_ai_alert_time', now);
        var tgChatId = global.get('telegram_chat_id');
        msgTg = {
            payload: {
                chatId: tgChatId,
                type: 'message',
                content: '🚨 *CẢNH BÁO SỚM ĐIỀU PHỐI GIAO THÔNG (AI MULTI-HORIZON)*\\n\\n' +
                         '📍 *Vị trí:* Cống 1 & Điểm trũng mặt đường\\n' +
                         '🌊 *Mực nước hiện tại:* ' + p.current_level + ' cm (' + p.current_fill + '%)\\n' +
                         '🔮 *Dự báo +5 phút:* ' + p.predicted_level_5m + ' cm\\n' +
                         '🔮 *Dự báo +30 phút:* ' + p.predicted_level_30m + ' cm (*Xác suất ngập: ' + p.prob_flood_30m + '%*)\\n' +
                         '🔮 *Dự báo +60 phút:* ' + p.predicted_level_60m + ' cm\\n\\n' +
                         '📢 *Khuyến nghị:* ' + p.traffic_advisory + '\\n\\n' +
                         '_(Tích hợp Radar Nowcasting & Cảm biến IoT UET)_'
            }
        };
    }
}

return [msgGauge5m, msgGauge30m, msgStatus, msgTraffic, [msgChartActual, msgChart5m, msgChart30m, msgChart60m], msgTg];"""

fn_process_ai = {
    "id": "fn_process_ai",
    "type": "function",
    "z": "tab_flood_main",
    "name": "Process Multi-Horizon AI",
    "func": js_code,
    "outputs": 6,
    "noerr": 0,
    "x": 360,
    "y": 480,
    "wires": [
        ["ui_gauge_ai_5m"],
        ["ui_gauge_ai_30m"],
        ["ui_text_ai_status"],
        ["ui_text_traffic_advisory"],
        ["ui_chart_ai_compare"],
        ["telegram_sender"]
    ]
}
flows.append(fn_process_ai)

ui_gauge_ai_5m = {
    "id": "ui_gauge_ai_5m",
    "type": "ui_gauge",
    "z": "tab_flood_main",
    "name": "Dự báo sau 5 phút",
    "group": "ui_group_ai",
    "order": 1,
    "width": "3",
    "height": "4",
    "gtype": "gage",
    "title": "Dự Báo +5 Phút (Tại Chỗ)",
    "label": "cm",
    "format": "{{value}}",
    "min": 0,
    "max": 180,
    "colors": ["#2ecc71", "#f39c12", "#e74c3c"],
    "seg1": "90",
    "seg2": "144",
    "x": 640,
    "y": 420,
    "wires": []
}
flows.append(ui_gauge_ai_5m)

ui_gauge_ai_30m = {
    "id": "ui_gauge_ai_30m",
    "type": "ui_gauge",
    "z": "tab_flood_main",
    "name": "Nguy cơ ngập sau 30 phút",
    "group": "ui_group_ai",
    "order": 2,
    "width": "3",
    "height": "4",
    "gtype": "gage",
    "title": "Nguy Cơ Ngập +30m (Giao Thông)",
    "label": "%",
    "format": "{{value}}%",
    "min": 0,
    "max": 100,
    "colors": ["#2ecc71", "#f39c12", "#e74c3c"],
    "seg1": "50",
    "seg2": "75",
    "x": 640,
    "y": 460,
    "wires": []
}
flows.append(ui_gauge_ai_30m)

ui_text_status = {
    "id": "ui_text_ai_status",
    "type": "ui_text",
    "z": "tab_flood_main",
    "group": "ui_group_ai",
    "order": 3,
    "width": "6",
    "height": "4",
    "name": "AI Status Multi-Horizon",
    "label": "Trạng Thái Đa Tầng Thời Gian:",
    "format": "{{{msg.payload}}}",
    "layout": "row-spread",
    "x": 640,
    "y": 500,
    "wires": []
}
flows.append(ui_text_status)

ui_text_traffic = {
    "id": "ui_text_traffic_advisory",
    "type": "ui_text",
    "z": "tab_flood_main",
    "group": "ui_group_ai",
    "order": 4,
    "width": "12",
    "height": "2",
    "name": "Traffic Advisory",
    "label": "Điều Phối Giao Thông & Vận Hành Hạ Tầng:",
    "format": "{{{msg.payload}}}",
    "layout": "row-spread",
    "x": 640,
    "y": 540,
    "wires": []
}
flows.append(ui_text_traffic)

ui_chart_ai = {
    "id": "ui_chart_ai_compare",
    "type": "ui_chart",
    "z": "tab_flood_main",
    "group": "ui_group_ai",
    "order": 5,
    "width": "12",
    "height": "5",
    "name": "Biểu Đồ Xu Hướng Đa Tầng AI",
    "label": "So Sánh Mực Nước Thực Tế vs Dự Báo Đa Mốc (+5m, +30m, +60m)",
    "chartType": "line",
    "legend": "true",
    "xformat": "HH:mm:ss",
    "interpolate": "linear",
    "nodata": "Đang chờ dữ liệu AI Multi-Horizon...",
    "dot": False,
    "ymin": "0",
    "ymax": "180",
    "removeOlder": "30",
    "removeOlderPoints": "",
    "removeOlderUnit": "60",
    "cutout": 0,
    "useOneColor": False,
    "useUTC": False,
    "colors": ["#2c3e50", "#2ecc71", "#e67e22", "#e74c3c", "#9b59b6", "#34495e"],
    "x": 660,
    "y": 580,
    "wires": [[]]
}
flows.append(ui_chart_ai)

with open(flow_file, "w", encoding="utf-8") as f:
    json.dump(flows, f, indent=2, ensure_ascii=False)

shutil.copyfile(flow_file, user_flow_file)
print(f"Thành công! Đã nâng cấp flow sang kiến trúc Multi-Horizon. Tổng số nodes: {len(flows)}")
