import json
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
FLOWS_PATH = BASE_DIR / "nodered" / "flows.json"
NODE_RED_DIR = BASE_DIR / "nodered" / ".node-red"
FLOWS_PATH_ACTIVE = NODE_RED_DIR / "flows.json"

with open(FLOWS_PATH, "r", encoding="utf-8") as f:
    nodes = json.load(f)

for n in nodes:
    nid = n.get("id")

    # 1. Fix fn_parse_cong1 (Anti-spike rate clamp)
    if nid == "fn_parse_cong1":
        n["func"] = (
            "// ── Validate payload ────────────────────────────────────────────────────\n"
            "var p = msg.payload;\n"
            "if (typeof p !== 'object' || p === null) {\n"
            "    node.warn('cong1: invalid payload');\n"
            "    return null;\n"
            "}\n\n"
            "var levelCm  = parseFloat(p.level_cm) || 0;\n"
            "levelCm = Math.max(0, Math.min(180, levelCm));\n"
            "var fillPct  = parseFloat(p.fill_pct) || ((levelCm / 180.0) * 100);\n"
            "fillPct = Math.max(0, Math.min(100, fillPct));\n"
            "var status   = p.status || 'green';\n\n"
            "// ── Rate calculation with anti-spike protection ────────────────────────\n"
            "var now = Date.now();\n"
            "var prev = context.get('prev') || { level: levelCm, time: now, rate: 0.0 };\n"
            "var dtSec = (now - prev.time) / 1000.0;\n\n"
            "var calcRate = prev.rate;\n"
            "if (dtSec >= 2.0) {\n"
            "    var dtMin = dtSec / 60.0;\n"
            "    calcRate = (levelCm - prev.level) / dtMin;\n"
            "    calcRate = Math.max(-25.0, Math.min(25.0, calcRate));\n"
            "    calcRate = Math.round(calcRate * 10) / 10;\n"
            "    context.set('prev', { level: levelCm, time: now, rate: calcRate });\n"
            "} else if (p.rise_rate !== undefined && !isNaN(parseFloat(p.rise_rate))) {\n"
            "    calcRate = Math.max(-25.0, Math.min(25.0, parseFloat(p.rise_rate)));\n"
            "    calcRate = Math.round(calcRate * 10) / 10;\n"
            "}\n\n"
            "// ── Store for downstream nodes & flow context ──────────────────────────\n"
            "flow.set('cong1_level',   levelCm);\n"
            "flow.set('cong1_fill',    fillPct);\n"
            "flow.set('cong1_rate',    calcRate);\n"
            "flow.set('cong1_status',  status);\n\n"
            "// ── Build enriched payload ───────────────────────────────────────────────\n"
            "msg.payload = {\n"
            "    level_cm : Math.round(levelCm * 10) / 10,\n"
            "    fill_pct : Math.round(fillPct * 10) / 10,\n"
            "    rise_rate: calcRate,\n"
            "    status   : status,\n"
            "    timestamp: now\n"
            "};\n\n"
            "return msg;\n"
        )

    # 2. Fix fn_hysteresis_cong1 (5 outputs for backend, gauge, status, rate, chart)
    elif nid == "fn_hysteresis_cong1":
        n["outputs"] = 5
        n["wires"] = [
            ["fn_combined_alert", "fn_thingspeak_cong1"],
            ["ui_gauge_fill"],
            ["ui_text_cong1_status"],
            ["ui_text_rise_rate"],
            ["ui_chart_cong1"]
        ]
        n["func"] = (
            "var p       = msg.payload;\n"
            "var levelCm = parseFloat(p.level_cm) || 0;\n"
            "var fillPct = parseFloat(p.fill_pct) || 0;\n"
            "var rate    = parseFloat(p.rise_rate) || 0;\n\n"
            "// State: 0=green, 1=yellow, 2=red\n"
            "var state   = flow.get('cong1_alert_state') || 0;\n\n"
            "// Rate override → RED immediately if rapid rise > 5 cm/min\n"
            "if (rate > 5) {\n"
            "    state = 2;\n"
            "} else {\n"
            "    switch (state) {\n"
            "        case 0: // GREEN\n"
            "            if (fillPct >= 60) state = 1;\n"
            "            break;\n"
            "        case 1: // YELLOW\n"
            "            if      (fillPct < 50) state = 0;\n"
            "            else if (fillPct >= 80) state = 2;\n"
            "            break;\n"
            "        case 2: // RED\n"
            "            if (fillPct < 70) state = 1;\n"
            "            break;\n"
            "    }\n"
            "}\n\n"
            "flow.set('cong1_alert_state', state);\n\n"
            "var stateStr   = ['green','yellow','red'][state];\n"
            "var stateVN    = ['AN TOÀN','CẢNH BÁO','NGUY HIỂM'][state];\n"
            "var stateColor = ['#2ecc71','#f39c12','#e74c3c'][state];\n\n"
            "p.alert_state  = state;\n"
            "p.status       = stateStr;\n"
            "p.status_vn    = stateVN;\n"
            "p.state_color  = stateColor;\n\n"
            "flow.set('cong1_status', stateStr);\n\n"
            "// Output 1: Object for backend decisions & ThingSpeak\n"
            "var msgData = { payload: p };\n\n"
            "// Output 2: Pure number float for Gauge Fill %\n"
            "var msgGauge = { payload: Math.round(fillPct * 10) / 10 };\n\n"
            "// Output 3: Status string for UI text\n"
            "var msgStatus = { payload: stateVN };\n\n"
            "// Output 4: Rate string for UI text\n"
            "var rateStr = (rate >= 0 ? '+' : '') + rate.toFixed(1) + ' cm/phút';\n"
            "var msgRate = { payload: rateStr };\n\n"
            "// Output 5: Data points for History Chart\n"
            "var msgChartLevel = { topic: 'Mực nước (cm)', payload: Math.round(levelCm * 10) / 10 };\n"
            "var msgChartFill  = { topic: 'Độ đầy (%)', payload: Math.round(fillPct * 10) / 10 };\n\n"
            "return [msgData, msgGauge, msgStatus, msgRate, [msgChartLevel, msgChartFill]];\n"
        )

    # 3. Fix fn_parse_duong1
    elif nid == "fn_parse_duong1":
        n["func"] = (
            "var p = msg.payload;\n"
            "if (typeof p !== 'object' || p === null) {\n"
            "    node.warn('duong1: invalid payload');\n"
            "    return null;\n"
            "}\n\n"
            "var depthCm = parseFloat(p.depth_cm) || 0;\n"
            "depthCm = Math.max(0, Math.min(100, depthCm));\n"
            "var status  = p.status || 'green';\n\n"
            "flow.set('duong1_depth',  depthCm);\n"
            "flow.set('duong1_status', status);\n\n"
            "msg.payload = {\n"
            "    depth_cm : Math.round(depthCm * 10) / 10,\n"
            "    status   : status,\n"
            "    timestamp: Date.now()\n"
            "};\n\n"
            "return msg;\n"
        )

    # 4. Fix fn_hysteresis_duong1 (4 outputs for backend, gauge, status, chart)
    elif nid == "fn_hysteresis_duong1":
        n["outputs"] = 4
        n["wires"] = [
            ["fn_combined_alert", "fn_thingspeak_duong1"],
            ["ui_gauge_depth"],
            ["ui_text_duong1_status"],
            ["ui_chart_duong1"]
        ]
        n["func"] = (
            "var p     = msg.payload;\n"
            "var depth = parseFloat(p.depth_cm) || 0;\n\n"
            "var state = flow.get('duong1_alert_state') || 0;\n\n"
            "switch (state) {\n"
            "    case 0: // GREEN\n"
            "        if (depth >= 5)  state = 1;\n"
            "        break;\n"
            "    case 1: // YELLOW\n"
            "        if      (depth < 3)  state = 0;\n"
            "        else if (depth >= 15) state = 2;\n"
            "        break;\n"
            "    case 2: // RED\n"
            "        if (depth < 10) state = 1;\n"
            "        break;\n"
            "}\n\n"
            "flow.set('duong1_alert_state', state);\n\n"
            "var stateStr   = ['green','yellow','red'][state];\n"
            "var stateVN    = ['AN TOÀN','CẢNH BÁO','NGẬP'][state];\n"
            "var stateColor = ['#2ecc71','#f39c12','#e74c3c'][state];\n\n"
            "p.alert_state = state;\n"
            "p.status      = stateStr;\n"
            "p.status_vn   = stateVN;\n"
            "p.state_color = stateColor;\n\n"
            "flow.set('duong1_status', stateStr);\n\n"
            "// Output 1: Object for combined alert & ThingSpeak\n"
            "var msgData = { payload: p };\n\n"
            "// Output 2: Pure number float for Gauge Depth cm\n"
            "var msgGauge = { payload: Math.round(depth * 10) / 10 };\n\n"
            "// Output 3: Status string for UI text\n"
            "var msgStatus = { payload: stateVN };\n\n"
            "// Output 4: Data point for History Chart\n"
            "var msgChart = { topic: 'Độ sâu ngập (cm)', payload: Math.round(depth * 10) / 10 };\n\n"
            "return [msgData, msgGauge, msgStatus, msgChart];\n"
        )

    # 5. Fix UI text formats
    elif nid == "ui_text_cong1_status":
        n["format"] = "{{msg.payload}}"
    elif nid == "ui_text_rise_rate":
        n["format"] = "{{msg.payload}}"
    elif nid == "ui_text_duong1_status":
        n["format"] = "{{msg.payload}}"

    # 6. Fix UI gauge formats
    elif nid == "ui_gauge_fill":
        n["format"] = "{{value}}%"
    elif nid == "ui_gauge_depth":
        n["format"] = "{{value}} cm"

    # 7. Fix UI charts Y-max
    elif nid == "ui_chart_cong1":
        n["ymin"] = "0"
        n["ymax"] = "180"

    # 8. Fix fn_process_ai
    elif nid == "fn_process_ai":
        n["func"] = (
            "var p = msg.payload;\n"
            "if (!p) return null;\n\n"
            "// Output 1: Gauge dự báo mực nước 5m (pure number)\n"
            "var msgGauge5m = {\n"
            "    payload: p.predicted_level_5m,\n"
            "    topic: 'Dự báo +5m'\n"
            "};\n\n"
            "// Output 2: Gauge xác suất ngập 30m (%) (pure number)\n"
            "var msgGauge30m = {\n"
            "    payload: p.prob_flood_30m,\n"
            "    topic: 'Xác suất ngập +30m (%)'\n"
            "};\n\n"
            "// Output 3: Trạng thái đa tầng (Object for UI Template)\n"
            "var msgStatus = {\n"
            "    payload: {\n"
            "        status_color: p.status_color || '#2ecc71',\n"
            "        predicted_status: p.predicted_status || 'AN TOÀN',\n"
            "        predicted_fill_5m: p.predicted_fill_5m || 0,\n"
            "        status_30m_color: p.status_30m_color || '#2ecc71',\n"
            "        status_30m: p.status_30m || 'AN TOÀN THOÁT NƯỚC',\n"
            "        prob_flood_30m: p.prob_flood_30m || 0,\n"
            "        current_level: p.current_level || 0,\n"
            "        predicted_level_15m: p.predicted_level_15m || 0,\n"
            "        predicted_level_30m: p.predicted_level_30m || 0,\n"
            "        predicted_level_60m: p.predicted_level_60m || 0\n"
            "    }\n"
            "};\n\n"
            "// Output 4: Khuyến nghị điều phối giao thông (Object for UI Template)\n"
            "var warnColor = p.early_warning ? '#e74c3c' : (p.prob_flood_30m >= 50 ? '#f39c12' : '#2ecc71');\n"
            "var msgTraffic = {\n"
            "    payload: {\n"
            "        traffic_color: warnColor,\n"
            "        traffic_advisory: p.traffic_advisory || 'Lưu lượng thoát ổn định.'\n"
            "    }\n"
            "};\n\n"
            "// Output 5: Biểu đồ đa đường (Actual, +5m, +30m, +60m)\n"
            "var msgChartActual = { topic: '1. Mực nước thực tế (cm)', payload: p.current_level };\n"
            "var msgChart5m     = { topic: '2. Dự báo AI +5m (cm)',   payload: p.predicted_level_5m };\n"
            "var msgChart30m    = { topic: '3. Dự báo AI +30m (cm)',  payload: p.predicted_level_30m };\n"
            "var msgChart60m    = { topic: '4. Dự báo AI +60m (cm)',  payload: p.predicted_level_60m };\n\n"
            "// Output 6: Telegram early warning alert\n"
            "var msgTg = null;\n"
            "if (p.early_warning) {\n"
            "    var lastAlert = flow.get('last_ai_alert_time') || 0;\n"
            "    var now = Date.now();\n"
            "    if (now - lastAlert > 180000) {\n"
            "        flow.set('last_ai_alert_time', now);\n"
            "        var tgChatId = global.get('telegram_chat_id');\n"
            "        msgTg = {\n"
            "            payload: {\n"
            "                chatId: tgChatId,\n"
            "                type: 'message',\n"
            "                content: '🚨 *CẢNH BÁO SỚM ĐIỀU PHỐI GIAO THÔNG (AI MULTI-HORIZON)*\\n\\n' +\n"
            "                         '📍 *Vị trí:* Cống 1 & Điểm trũng mặt đường\\n' +\n"
            "                         '🌊 *Mực nước hiện tại:* ' + p.current_level + ' cm (' + p.current_fill + '%)\\n' +\n"
            "                         '🔮 *Dự báo +5 phút:* ' + p.predicted_level_5m + ' cm\\n' +\n"
            "                         '🔮 *Dự báo +30 phút:* ' + p.predicted_level_30m + ' cm (*Xác suất ngập: ' + p.prob_flood_30m + '%*)\\n' +\n"
            "                         '🔮 *Dự báo +60 phút:* ' + p.predicted_level_60m + ' cm\\n\\n' +\n"
            "                         '📢 *Khuyến nghị:* ' + p.traffic_advisory + '\\n\\n' +\n"
            "                         '_(Tích hợp Radar Nowcasting & Cảm biến IoT UET)_'\n"
            "            }\n"
            "        };\n"
            "    }\n"
            "}\n\n"
            "return [msgGauge5m, msgGauge30m, msgStatus, msgTraffic, [msgChartActual, msgChart5m, msgChart30m, msgChart60m], msgTg];\n"
        )

    # 9. Convert AI text widgets to ui_template for rich HTML badges
    elif nid == "ui_text_ai_status":
        n["type"] = "ui_template"
        n["name"] = "Trạng Thái Đa Tầng AI"
        n["format"] = (
            '<div style="display:flex; flex-direction:column; justify-content:center; height:100%; font-size:13px; line-height:1.7; padding:4px;">'
            '<div style="margin-bottom:6px;"><b>Mốc +5m:</b> <span style="display:inline-block; padding:2px 10px; border-radius:12px; color:#fff; font-weight:bold; background:{{msg.payload.status_color}};">{{msg.payload.predicted_status}} ({{msg.payload.predicted_fill_5m}}%)</span></div>'
            '<div style="margin-bottom:6px;"><b>Mốc +30m:</b> <span style="display:inline-block; padding:2px 10px; border-radius:12px; color:#fff; font-weight:bold; background:{{msg.payload.status_30m_color}};">{{msg.payload.status_30m}} ({{msg.payload.prob_flood_30m}}%)</span></div>'
            '<div style="color:#555;"><b>Mực nước:</b> Hiện tại: <b>{{msg.payload.current_level}}cm</b> | +15m: <b>{{msg.payload.predicted_level_15m}}cm</b> | +30m: <b>{{msg.payload.predicted_level_30m}}cm</b> | +60m: <b>{{msg.payload.predicted_level_60m}}cm</b></div>'
            '</div>'
        )
        n["templateScope"] = "local"
    elif nid == "ui_text_traffic_advisory":
        n["type"] = "ui_template"
        n["name"] = "Khuyến Nghị Điều Phối Giao Thông"
        n["format"] = (
            '<div style="display:flex; align-items:center; height:100%; padding:8px 14px; border-left:5px solid {{msg.payload.traffic_color}}; background:#f8f9fa; font-weight:600; color:{{msg.payload.traffic_color}}; font-size:14px; border-radius:4px;">'
            '🚦 {{msg.payload.traffic_advisory}}'
            '</div>'
        )
        n["templateScope"] = "local"

with open(FLOWS_PATH, "w", encoding="utf-8") as f:
    json.dump(nodes, f, indent=2, ensure_ascii=False)

with open(FLOWS_PATH_ACTIVE, "w", encoding="utf-8") as f:
    json.dump(nodes, f, indent=2, ensure_ascii=False)

print("✅ Đã cập nhật thành công cả 2 file flows.json!")
