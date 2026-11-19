/**
 * Gateway ↔ Cloud link — chạy trên laptop gateway, cạnh Mosquitto + Node-RED.
 *
 *  Mosquitto cục bộ (localhost:1883) ──[topic đã xử lý tại Edge]──▶ wss://<cloud>/mqtt (broker tích hợp trong server.js)
 *  Mosquitto cục bộ                ◀──[lệnh flood/cloud/#]──────────  cloud
 *
 * Làm đúng việc của Mosquitto bridge (cloud-bridge.conf), nhưng đi qua HTTPS/WebSocket (cổng 443, có mã hoá TLS)
 * để dùng được với nền tảng cloud chỉ mở cổng web (Render.com, Cloudflare Tunnel…).
 *  - Chỉ gửi dữ liệu đã xử lý: flood/edge/state|log|incident|alert, flood/+/info|status, flood/ai/prediction.
 *    Dữ liệu thô flood/<id>/level KHÔNG lên cloud.
 *  - Lệnh từ cloud chỉ vào flood/cloud/# — Node-RED kiểm duyệt rồi mới chuyển xuống node.
 *  - flood/edge/bridge = 1 khi kết nối; Last Will = 0 → cloud biết ngay khi gateway mất kết nối.
 *
 * Chạy: node cloud_link.js   (đọc config.json cùng thư mục, hoặc biến môi trường CLOUD_URL / EDGE_PASSWORD)
 */
'use strict';
const fs = require('fs');
const path = require('path');
const mqtt = require('mqtt');

const cfgFile = path.join(__dirname, 'config.json');
const fileCfg = fs.existsSync(cfgFile) ? JSON.parse(fs.readFileSync(cfgFile, 'utf8')) : {};
const CFG = {
    url: process.env.CLOUD_URL || fileCfg.url,
    username: process.env.EDGE_USER || fileCfg.username || 'edge',
    password: process.env.EDGE_PASSWORD || fileCfg.password,
    local: process.env.LOCAL_MQTT || fileCfg.local || 'mqtt://localhost:1883'
};
if (!CFG.url || !CFG.password || /doi-mat-khau|ten-tai-khoan/.test(CFG.url + CFG.password)) {
    console.error('Thiếu cấu hình: sao chép config.example.json → config.json rồi điền url và password.');
    process.exit(1);
}

const UP = ['flood/edge/state', 'flood/edge/log', 'flood/edge/incident', 'flood/edge/alert',
    'flood/+/info', 'flood/+/status', 'flood/ai/prediction'];
const DOWN = 'flood/cloud/#';
const BRIDGE = 'flood/edge/bridge';
const ts = () => new Date().toLocaleTimeString('vi-VN');
const log = (...a) => console.log(`[${ts()}]`, ...a);

const local = mqtt.connect(CFG.local, {
    clientId: 'flood-cloud-link-' + Math.random().toString(16).slice(2, 8),
    protocolVersion: 5, reconnectPeriod: 3000
});
const remote = mqtt.connect(CFG.url, {
    clientId: 'flood-gateway-' + Math.random().toString(16).slice(2, 8),
    username: CFG.username, password: CFG.password,
    keepalive: 20, reconnectPeriod: 5000, connectTimeout: 15000,
    queueQoSZero: false,                                   // không dồn số đo cũ khi mất mạng; cảnh báo QoS 1 vẫn được giữ
    will: { topic: BRIDGE, payload: '0', qos: 1, retain: true }
});

let up = 0, down = 0;
function subscribeLocal() {
    // retain-as-published: giữ cờ retained (trạng thái, info) để cloud nhận đúng như Mosquitto bridge
    local.subscribe(UP.map(t => ({ [t]: { qos: 1, rap: true } })).reduce((a, b) => ({ ...a, ...b }), {}),
        err => err && log('[LOCAL] Lỗi subscribe:', err.message));
}

local.on('connect', () => { log('[LOCAL] Đã kết nối', CFG.local); if (remote.connected) subscribeLocal(); });
local.on('error', e => log('[LOCAL] Lỗi:', e.message || e.code || (e.errors && e.errors[0] && e.errors[0].code) || String(e), '— Mosquitto đã chạy chưa?'));
local.on('offline', () => log('[LOCAL] Mất kết nối Mosquitto cục bộ'));

remote.on('connect', () => {
    log('[CLOUD] Đã kết nối', CFG.url);
    remote.publish(BRIDGE, '1', { qos: 1, retain: true });
    remote.subscribe(DOWN, { qos: 1 });
    if (local.connected) { local.unsubscribe(UP, () => subscribeLocal()); }   // gửi lại trạng thái retained mới nhất
});
remote.on('error', e => log('[CLOUD]', e.message));
remote.on('offline', () => log('[CLOUD] Mất kết nối — tự thử lại sau 5 s'));

local.on('message', (topic, payload, packet) => {
    if (!remote.connected) return;
    remote.publish(topic, payload, { qos: Math.min(packet.qos, 1), retain: !!packet.retain });
    up++;
});
remote.on('message', (topic, payload) => {
    if (!topic.startsWith('flood/cloud/')) return;
    local.publish(topic, payload, { qos: 1 });
    down++;
    log('[CLOUD → GATEWAY]', topic);
});

setInterval(() => log(`[THỐNG KÊ] lên cloud: ${up} tin · lệnh từ cloud: ${down} · cloud ${remote.connected ? 'ONLINE' : 'OFFLINE'}`), 60000);

// Giữ cloud gói free (Render…) không ngủ khi gateway đang chạy: gọi /healthz mỗi 5 phút.
// Đồng thời đánh thức cloud ngay khi gateway khởi động, để lần kết nối wss đầu không phải chờ.
const healthUrl = CFG.url.replace(/^ws(s?):\/\//, 'http$1://').replace(/\/mqtt\/?$/, '/healthz');
const ping = () => fetch(healthUrl, { signal: AbortSignal.timeout(90000) })
    .then(r => { if (!r.ok) log('[CLOUD] /healthz trả về', r.status); })
    .catch(e => log('[CLOUD] Không gọi được /healthz:', e.message));
if (/^https?:\/\/(?!localhost|127\.)/.test(healthUrl)) { ping(); setInterval(ping, 5 * 60000); }

function shutdown() {
    log('Đang dừng — báo cloud gateway offline');
    const done = () => { remote.end(false, () => local.end(false, () => process.exit(0))); };
    if (remote.connected) remote.publish(BRIDGE, '0', { qos: 1, retain: true }, done); else done();
    setTimeout(() => process.exit(0), 3000);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
