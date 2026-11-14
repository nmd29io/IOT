/**
 * Cloud server — Hệ thống IoT giám sát & cảnh báo ngập (Stage 4: Data Center / Cloud)
 *
 *  Gateway (Node-RED) ──MQTT bridge──▶ Mosquitto (cloud) ──▶ server.js ──▶ SQLite
 *                                                              │
 *                                       REST API + WebSocket ◀─┴─▶ Web app (public/)
 *
 * Nhận (topic do gateway bridge lên):
 *   flood/edge/state      trạng thái đã xử lý tại Edge (retained)
 *   flood/edge/log        nhật ký sự kiện
 *   flood/edge/incident   sự cố đã đóng (thời lượng, thời gian phản hồi, báo trước)
 *   flood/edge/bridge     1/0 — kết nối gateway ↔ cloud (Mosquitto bridge notification)
 *   flood/edge/alert      cảnh báo → lưu + Web Push tới điện thoại người quản lý (PWA)
 *   flood/+/info, flood/+/status, flood/ai/prediction
 * Gửi xuống gateway (chỉ topic flood/cloud/#, gateway kiểm duyệt rồi mới chuyển cho node):
 *   flood/cloud/ack, flood/cloud/config/<id>, flood/cloud/cmd/<id>, flood/cloud/alarm/<id>
 */
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const crypto = require('crypto');
const express = require('express');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');
const { DatabaseSync } = require('node:sqlite');
const webpush = require('web-push');

const CFG = {
    port: Number(process.env.PORT) || 8080,
    mqttUrl: process.env.MQTT_URL || 'mqtt://localhost:1883',
    mqttUser: process.env.MQTT_USER || undefined,
    mqttPass: process.env.MQTT_PASSWORD || undefined,
    adminToken: process.env.ADMIN_TOKEN || '',
    dbPath: process.env.DB_PATH || path.join(__dirname, 'data', 'flood.db'),
    sampleMs: 5000,          // lưu tối đa 1 mẫu / 5 s / node
    keepDays: 30
};
if (!CFG.adminToken) console.warn('[WARN] ADMIN_TOKEN chưa đặt — các chức năng quản trị bị khoá');

const nodesMeta = JSON.parse(fs.readFileSync(path.join(__dirname, 'nodes.json'), 'utf8'));
delete nodesMeta._ghi_chu;
const ID_RE = /^[a-z0-9_-]{1,20}$/;

// ─── Cơ sở dữ liệu ───────────────────────────────────────────────────────────
fs.mkdirSync(path.dirname(CFG.dbPath), { recursive: true });
const db = new DatabaseSync(CFG.dbPath);
db.exec(`
CREATE TABLE IF NOT EXISTS measurements (ts INTEGER, node TEXT, kind TEXT, level TEXT, m REAL, L REAL, v REAL, h REAL);
CREATE INDEX IF NOT EXISTS idx_meas ON measurements(node, ts);
CREATE TABLE IF NOT EXISTS events (ts INTEGER, text TEXT);
CREATE INDEX IF NOT EXISTS idx_events ON events(ts);
CREATE TABLE IF NOT EXISTS incidents (key TEXT PRIMARY KEY, node TEXT, start INTEGER, end INTEGER, duration_s INTEGER,
    ack INTEGER, ack_by TEXT, response_s INTEGER, reminders INTEGER, max_L REAL, lead_s INTEGER);
CREATE TABLE IF NOT EXISTS kv (key TEXT PRIMARY KEY, value TEXT, ts INTEGER);
CREATE TABLE IF NOT EXISTS alerts (ts INTEGER, severity TEXT, title TEXT, body TEXT, node TEXT, key TEXT);
CREATE INDEX IF NOT EXISTS idx_alerts ON alerts(ts);
CREATE TABLE IF NOT EXISTS push_subs (endpoint TEXT PRIMARY KEY, sub TEXT, name TEXT, created INTEGER);
`);
const Q = {
    insMeas: db.prepare('INSERT INTO measurements VALUES (?,?,?,?,?,?,?,?)'),
    insEvent: db.prepare('INSERT INTO events VALUES (?,?)'),
    insInc: db.prepare('INSERT OR REPLACE INTO incidents VALUES (?,?,?,?,?,?,?,?,?,?,?)'),
    setKv: db.prepare('INSERT OR REPLACE INTO kv VALUES (?,?,?)'),
    kvLike: db.prepare('SELECT key, value, ts FROM kv WHERE key LIKE ?'),
    history: db.prepare('SELECT ts, level, m, L, v, h FROM measurements WHERE node = ? AND ts >= ? ORDER BY ts'),
    events: db.prepare('SELECT ts, text FROM events ORDER BY ts DESC LIMIT ?'),
    incidents: db.prepare('SELECT * FROM incidents ORDER BY start DESC LIMIT ?'),
    stats: db.prepare(`SELECT COUNT(*) AS n, AVG(response_s) AS avg_response_s, AVG(duration_s) AS avg_duration_s,
        AVG(lead_s) AS avg_lead_s, SUM(CASE WHEN ack = 1 THEN 0 ELSE 1 END) AS unacked FROM incidents WHERE start >= ?`),
    pruneMeas: db.prepare('DELETE FROM measurements WHERE ts < ?'),
    pruneEvents: db.prepare('DELETE FROM events WHERE ts < ?'),
    insAlert: db.prepare('INSERT INTO alerts VALUES (?,?,?,?,?,?)'),
    alerts: db.prepare('SELECT * FROM alerts ORDER BY ts DESC LIMIT ?'),
    pruneAlerts: db.prepare('DELETE FROM alerts WHERE ts < ?'),
    getKv: db.prepare('SELECT value FROM kv WHERE key = ?'),
    addSub: db.prepare('INSERT OR REPLACE INTO push_subs VALUES (?,?,?,?)'),
    delSub: db.prepare('DELETE FROM push_subs WHERE endpoint = ?'),
    getSub: db.prepare('SELECT * FROM push_subs WHERE endpoint = ?'),
    allSubs: db.prepare('SELECT * FROM push_subs')
};
const nul = v => (v === undefined || v === null || Number.isNaN(v) ? null : v);

// ─── Trạng thái trong bộ nhớ (khôi phục từ kv khi khởi động lại) ─────────────
const live = { edge: null, edgeRecv: 0, bridge: null, info: {}, status: {}, ai: null };
for (const row of Q.kvLike.all('%')) {
    const v = JSON.parse(row.value);
    if (row.key === 'edge') { live.edge = v; live.edgeRecv = row.ts; }
    else if (row.key === 'bridge') live.bridge = v;
    else if (row.key === 'ai') live.ai = v;
    else if (row.key.startsWith('info:')) live.info[row.key.slice(5)] = v;
    else if (row.key.startsWith('status:')) live.status[row.key.slice(7)] = v;
}
const lastSampled = {};
const saveKv = (k, v) => Q.setKv.run(k, JSON.stringify(v), Date.now());

// ─── Web Push (VAPID): khoá tạo 1 lần, lưu trong DB ─────────────────────────
let vapid = process.env.VAPID_PUBLIC_KEY && process.env.VAPID_PRIVATE_KEY
    ? { publicKey: process.env.VAPID_PUBLIC_KEY, privateKey: process.env.VAPID_PRIVATE_KEY } : null;
if (!vapid) {
    const row = Q.getKv.get('vapid');
    vapid = row ? JSON.parse(row.value) : webpush.generateVAPIDKeys();
    if (!row) Q.setKv.run('vapid', JSON.stringify(vapid), Date.now());
}
webpush.setVapidDetails(process.env.VAPID_SUBJECT || 'mailto:flood-monitor@example.com', vapid.publicKey, vapid.privateKey);

async function pushToAll(payload) {
    const subs = Q.allSubs.all();
    const body = JSON.stringify(payload);
    await Promise.all(subs.map(async row => {
        try {
            await webpush.sendNotification(JSON.parse(row.sub), body, { TTL: 600, urgency: payload.severity === 'critical' ? 'high' : 'normal' });
        } catch (e) {
            if (e.statusCode === 404 || e.statusCode === 410) Q.delSub.run(row.endpoint);   // thiết bị đã huỷ đăng ký
            else console.error('[PUSH]', e.statusCode || '', e.message);
        }
    }));
    return subs.length;
}

// ─── WebSocket ───────────────────────────────────────────────────────────────
let wss;
function broadcast(type, data, adminOnly = false) {
    if (!wss) return;
    const text = JSON.stringify({ type, data });
    for (const ws of wss.clients) {
        if (ws.readyState === 1 && (!adminOnly || ws.isAdmin)) ws.send(text);
    }
}

// ─── MQTT ────────────────────────────────────────────────────────────────────
const client = mqtt.connect(CFG.mqttUrl, {
    username: CFG.mqttUser, password: CFG.mqttPass,
    clientId: 'flood-cloud-server-' + crypto.randomBytes(3).toString('hex'),
    reconnectPeriod: 3000
});
client.on('connect', () => {
    console.log('[MQTT] Đã kết nối', CFG.mqttUrl);
    client.subscribe(['flood/edge/#', 'flood/+/info', 'flood/+/status', 'flood/ai/prediction'], { qos: 1 });
});
client.on('error', e => console.error('[MQTT]', e.message));
client.on('message', (topic, buf) => {
    try { handleMessage(topic, buf.toString()); } catch (e) { console.error('[MQTT] Lỗi xử lý', topic, e.message); }
});

function sanitizeInfo(info) {
    if (!info) return info;
    const { ip, heap, ...rest } = info;    // không công khai IP nội bộ
    return rest;
}

function handleMessage(topic, text) {
    const parts = topic.split('/');
    const now = Date.now();
    const json = () => { try { return JSON.parse(text); } catch { return null; } };

    if (topic === 'flood/edge/state') {
        const st = json();
        if (!st || typeof st !== 'object') return;
        live.edge = st; live.edgeRecv = now; saveKv('edge', st);
        for (const [id, n] of Object.entries(st.nodes || {})) {
            if (!n.data || !n.last_seen || !ID_RE.test(id)) continue;
            if (n.last_seen - (lastSampled[id] || 0) < CFG.sampleMs) continue;
            lastSampled[id] = n.last_seen;
            const d = n.data;
            Q.insMeas.run(n.last_seen, id, nul(n.kind), nul(n.level), nul(d.m), nul(d.L), nul(d.v), nul(d.h));
        }
        broadcast('state', st);
    } else if (topic === 'flood/edge/log') {
        const e = json();
        if (!e || !e.text) return;
        Q.insEvent.run(Number(e.t) || now, String(e.text).slice(0, 500));
        broadcast('log', { ts: Number(e.t) || now, text: e.text }, true);
    } else if (topic === 'flood/edge/incident') {
        const i = json();
        if (!i || !i.key) return;
        Q.insInc.run(i.key, nul(i.node), nul(i.start), nul(i.end), nul(i.duration_s), i.ack ? 1 : 0, nul(i.ack_by),
            nul(i.response_s), nul(i.reminders), nul(i.max_L), nul(i.lead_s));
        broadcast('incident', i, true);
    } else if (topic === 'flood/edge/alert') {
        const a = json();
        if (!a || !a.title) return;
        const ts = Number(a.ts) || now;
        const alert = { ts, severity: String(a.severity || 'info'), title: String(a.title).slice(0, 200),
            body: String(a.body || '').slice(0, 600), node: a.node || null, key: a.key || null };
        Q.insAlert.run(alert.ts, alert.severity, alert.title, alert.body, alert.node, alert.key);
        broadcast('alert', alert, true);
        if (alert.severity !== 'info') pushToAll({ ...alert, url: '/admin.html' }).catch(e => console.error('[PUSH]', e.message));
    } else if (topic === 'flood/edge/bridge') {
        live.bridge = { connected: text.trim() === '1', ts: now };
        saveKv('bridge', live.bridge);
        broadcast('gateway', gatewayStatus());
    } else if (parts.length === 3 && parts[2] === 'info' && ID_RE.test(parts[1])) {
        const i = json();
        if (!i) return;
        live.info[parts[1]] = { ...i, received: now };
        saveKv('info:' + parts[1], live.info[parts[1]]);
        broadcast('info', { id: parts[1], info: sanitizeInfo(live.info[parts[1]]) });
    } else if (parts.length === 3 && parts[2] === 'status' && ID_RE.test(parts[1])) {
        live.status[parts[1]] = { status: text.trim(), ts: now };
        saveKv('status:' + parts[1], live.status[parts[1]]);
    } else if (topic === 'flood/ai/prediction') {
        const a = json();
        if (!a) return;
        live.ai = {
            ts: now, current_level: a.current_level, predicted_level_5m: a.predicted_level_5m,
            predicted_level_30m: a.predicted_level_30m, prob_flood_30m: a.prob_flood_30m,
            predicted_status: a.predicted_status, status_30m: a.status_30m
        };
        saveKv('ai', live.ai);
        broadcast('ai', live.ai);
    }
}

// Gateway coi là online khi bridge báo kết nối VÀ trạng thái Edge mới cập nhật trong 60 s.
function gatewayStatus() {
    const age = live.edgeRecv ? Math.round((Date.now() - live.edgeRecv) / 1000) : null;
    const bridgeOk = live.bridge ? live.bridge.connected : null;
    return { bridge_connected: bridgeOk, edge_age_s: age, online: bridgeOk !== false && age !== null && age <= 60 };
}

// ─── HTTP API ────────────────────────────────────────────────────────────────
const app = express();
app.disable('x-powered-by');
app.use(express.json({ limit: '16kb' }));
app.use(express.static(path.join(__dirname, 'public'), { extensions: ['html'] }));

function checkToken(t) {
    if (!CFG.adminToken || typeof t !== 'string') return false;
    const a = Buffer.from(t), b = Buffer.from(CFG.adminToken);
    return a.length === b.length && crypto.timingSafeEqual(a, b);
}
const isAdmin = req => checkToken(req.get('x-admin-token'));
function requireAdmin(req, res, next) {
    if (!isAdmin(req)) return res.status(401).json({ error: 'Sai hoặc thiếu mã quản trị' });
    next();
}
function publish(topic, payload, res, okText) {
    if (!client.connected) return res.status(503).json({ error: 'Cloud chưa kết nối được broker' });
    client.publish(topic, typeof payload === 'string' ? payload : JSON.stringify(payload), { qos: 1 }, err => {
        if (err) return res.status(500).json({ error: err.message });
        res.json({ ok: true, message: okText });
    });
}

app.get('/api/state', (req, res) => {
    const admin = isAdmin(req);
    const info = Object.fromEntries(Object.entries(live.info).map(([k, v]) => [k, admin ? v : sanitizeInfo(v)]));
    res.json({
        server_ts: Date.now(), meta: nodesMeta, edge: live.edge, gateway: gatewayStatus(),
        info, status: live.status, ai: live.ai
    });
});

app.get('/api/history', (req, res) => {
    const node = String(req.query.node || '');
    if (!ID_RE.test(node)) return res.status(400).json({ error: 'node không hợp lệ' });
    const hours = Math.min(72, Math.max(0.25, Number(req.query.hours) || 6));
    const rows = Q.history.all(node, Date.now() - hours * 3600e3);
    const step = Math.ceil(rows.length / 1500) || 1;    // giới hạn ~1500 điểm cho biểu đồ
    res.json({ node, hours, rows: rows.filter((_, i) => i % step === 0) });
});

app.post('/api/login', (req, res) => (checkToken(req.body && req.body.token) ? res.json({ ok: true }) : res.status(401).json({ error: 'Sai mã quản trị' })));

app.get('/api/events', requireAdmin, (req, res) => {
    res.json(Q.events.all(Math.min(200, Number(req.query.limit) || 50)));
});

app.get('/api/incidents', requireAdmin, (req, res) => {
    const since = Date.now() - 7 * 86400e3;
    res.json({ stats: Q.stats.get(since), items: Q.incidents.all(Math.min(100, Number(req.query.limit) || 20)) });
});

app.post('/api/ack', requireAdmin, (req, res) => {
    const by = String((req.body && req.body.by) || 'Quản lý').slice(0, 40) + ' (Web)';
    const key = req.body && typeof req.body.key === 'string' ? req.body.key.slice(0, 60) : undefined;
    publish('flood/cloud/ack', { key, by }, res, 'Đã gửi xác nhận xuống gateway');
});

const CONFIG_KEYS = ['H', 'Hmax', 'd0', 't_yellow', 't_red', 't_red_low', 't_yellow_low', 'stable_s', 'period_s'];
app.post('/api/nodes/:id/config', requireAdmin, (req, res) => {
    const id = req.params.id;
    if (!ID_RE.test(id)) return res.status(400).json({ error: 'node không hợp lệ' });
    const clean = {};
    for (const k of CONFIG_KEYS) {
        const v = req.body ? req.body[k] : undefined;
        if (v === undefined || v === null || v === '') continue;
        const n = Number(v);
        if (!Number.isFinite(n)) return res.status(400).json({ error: `Giá trị ${k} không hợp lệ` });
        clean[k] = n;
    }
    if (!Object.keys(clean).length) return res.status(400).json({ error: 'Chưa có giá trị nào' });
    publish(`flood/cloud/config/${id}`, clean, res, 'Đã gửi cấu hình — gateway kiểm tra rồi chuyển xuống node');
});

app.post('/api/nodes/:id/cmd', requireAdmin, (req, res) => {
    const id = req.params.id, cmd = req.body && req.body.cmd;
    if (!ID_RE.test(id) || !['calibrate', 'report'].includes(cmd)) return res.status(400).json({ error: 'Lệnh không hợp lệ' });
    publish(`flood/cloud/cmd/${id}`, { cmd }, res, 'Đã gửi lệnh');
});

app.post('/api/nodes/:id/alarm', requireAdmin, (req, res) => {
    const id = req.params.id, action = req.body && req.body.action;
    if (!ID_RE.test(id) || !['off', 'on'].includes(action)) return res.status(400).json({ error: 'Lệnh không hợp lệ' });
    publish(`flood/cloud/alarm/${id}`, action, res, action === 'off' ? 'Đã gửi lệnh tắt còi' : 'Đã gửi lệnh bật lại còi');
});

// ─── Cảnh báo & Web Push ─────────────────────────────────────────────────────
app.get('/api/alerts', requireAdmin, (req, res) => res.json(Q.alerts.all(Math.min(100, Number(req.query.limit) || 30))));
app.get('/api/push/key', (req, res) => res.json({ publicKey: vapid.publicKey }));

app.post('/api/push/subscribe', requireAdmin, (req, res) => {
    const sub = req.body && req.body.subscription;
    if (!sub || typeof sub.endpoint !== 'string' || !/^https:\/\//.test(sub.endpoint) || !sub.keys) {
        return res.status(400).json({ error: 'Đăng ký không hợp lệ' });
    }
    const name = String((req.body && req.body.name) || 'Quản lý').slice(0, 40);
    Q.addSub.run(sub.endpoint, JSON.stringify(sub), name, Date.now());
    res.json({ ok: true, message: 'Đã bật thông báo trên thiết bị này' });
});

app.post('/api/push/unsubscribe', requireAdmin, (req, res) => {
    Q.delSub.run(String((req.body && req.body.endpoint) || ''));
    res.json({ ok: true, message: 'Đã tắt thông báo trên thiết bị này' });
});

app.post('/api/push/test', requireAdmin, async (req, res) => {
    const n = await pushToAll({ ts: Date.now(), severity: 'warning', title: '🔔 Thử thông báo', body: 'Thiết bị này sẽ nhận cảnh báo ngập.', url: '/admin.html' });
    res.json({ ok: true, message: `Đã gửi thử tới ${n} thiết bị` });
});

// Bấm "✅ Đã xử lý" ngay trên thông báo: service worker không có mã quản trị,
// nên xác thực bằng chính đăng ký push (chỉ thiết bị quản trị đã đăng ký mới có trong DB).
app.post('/api/push/ack', (req, res) => {
    const endpoint = String((req.body && req.body.endpoint) || '');
    const key = req.body && typeof req.body.key === 'string' ? req.body.key.slice(0, 60) : '';
    const row = endpoint && Q.getSub.get(endpoint);
    if (!row || !key) return res.status(403).json({ error: 'Thiết bị chưa đăng ký nhận thông báo' });
    publish('flood/cloud/ack', { key, by: row.name + ' (thông báo)' }, res, 'Đã gửi xác nhận');
});

app.get('/healthz', (req, res) => res.json({ ok: true, mqtt: client.connected, gateway: gatewayStatus() }));

// ─── Khởi động ───────────────────────────────────────────────────────────────
const server = http.createServer(app);
wss = new WebSocketServer({ server, path: '/ws' });
wss.on('connection', ws => {
    ws.isAdmin = false;
    ws.on('message', raw => {                         // client gửi {"auth":"<token>"} để nhận cả nhật ký
        try { const m = JSON.parse(raw); if (checkToken(m.auth)) ws.isAdmin = true; } catch { /* bỏ qua */ }
    });
});
setInterval(() => broadcast('gateway', gatewayStatus()), 15000);
setInterval(() => {
    const cut = Date.now() - CFG.keepDays * 86400e3;
    Q.pruneMeas.run(cut); Q.pruneEvents.run(cut); Q.pruneAlerts.run(cut);
}, 6 * 3600e3);

server.listen(CFG.port, () => console.log(`[HTTP] Web console: http://localhost:${CFG.port}  (quản trị: /admin)`));
