/**
 * Broker MQTT tích hợp trong server (chế độ BROKER=embedded) — dùng khi nền tảng cloud chỉ mở cổng HTTPS
 * (Render.com, Cloudflare Tunnel…). Gateway kết nối bằng MQTT qua WebSocket bảo mật: wss://<host>/mqtt
 *
 *  Gateway (cloud_link.js) ──wss://…/mqtt──▶ broker (aedes) ◀──luồng nội bộ── server.js
 *
 * Tài khoản & phân quyền (tương đương cloud/mosquitto/acl khi chạy bằng Mosquitto trên VM):
 *   edge   (mật khẩu EDGE_PASSWORD): ghi flood/edge/#, flood/+/info, flood/+/status, flood/ai/prediction; đọc flood/cloud/#
 *   server (mật khẩu ngẫu nhiên, chỉ trong tiến trình): đọc flood/#, ghi flood/cloud/#
 */
'use strict';
const crypto = require('crypto');
const { duplexPair } = require('stream');
const { createWebSocketStream } = require('ws');
const Aedes = require('aedes');

// Khớp topic theo quy tắc MQTT (+ một cấp, # nhiều cấp)
function match(filter, topic) {
    const f = filter.split('/'), t = topic.split('/');
    for (let i = 0; i < f.length; i++) {
        if (f[i] === '#') return true;
        if (i >= t.length || (f[i] !== '+' && f[i] !== t[i])) return false;
    }
    return f.length === t.length;
}
// Bộ lọc khi subscribe phải nằm trong quyền đọc: so khớp từng cấp, '#'/'+' của client chỉ được dùng nơi ACL cũng cho phép
function filterAllowed(rule, filter) {
    const r = rule.split('/'), f = filter.split('/');
    for (let i = 0; i < f.length; i++) {
        if (r[i] === '#') return true;
        if (i >= r.length) return false;
        if (r[i] === '+') { if (f[i] === '#') return false; continue; }
        if (f[i] !== r[i]) return false;
    }
    return f.length === r.length;
}

const ACL = {
    edge: { write: ['flood/edge/#', 'flood/+/info', 'flood/+/status', 'flood/ai/prediction'], read: ['flood/cloud/#'] },
    server: { write: ['flood/cloud/#'], read: ['flood/#'] }
};

function createEmbeddedBroker({ edgePassword, log = console }) {
    if (!edgePassword || edgePassword.length < 8) throw new Error('BROKER=embedded cần EDGE_PASSWORD (≥ 8 ký tự)');
    const serverPassword = crypto.randomBytes(24).toString('hex');
    const secrets = { edge: edgePassword, server: serverPassword };
    const aedes = Aedes({ id: 'flood-cloud' });

    const same = (a, b) => { const x = Buffer.from(a), y = Buffer.from(b); return x.length === y.length && crypto.timingSafeEqual(x, y); };
    aedes.authenticate = (client, username, password, cb) => {
        const pw = password ? password.toString() : '';
        const ok = Object.hasOwn(secrets, username || '') && same(pw, secrets[username]);
        if (ok) { client.user = username; return cb(null, true); }
        const err = new Error('Sai tài khoản MQTT'); err.returnCode = 4;
        log.warn('[BROKER] Từ chối đăng nhập', username || '(trống)');
        cb(err, false);
    };
    aedes.authorizePublish = (client, packet, cb) => {
        if (!client) return cb(null);                                   // tin của chính broker ($SYS, will đã duyệt)
        const acl = ACL[client.user];
        if (acl && acl.write.some(r => match(r, packet.topic))) return cb(null);
        cb(new Error('Không có quyền ghi ' + packet.topic));
    };
    aedes.authorizeSubscribe = (client, sub, cb) => {
        const acl = ACL[client.user];
        if (acl && acl.read.some(r => filterAllowed(r, sub.topic))) return cb(null, sub);
        cb(null, null);                                                // từ chối: SUBACK 0x80
    };
    aedes.on('client', c => { if (c.user === 'edge') log.log('[BROKER] Gateway đã kết nối:', c.id); });
    aedes.on('clientDisconnect', c => { if (c.user === 'edge') log.log('[BROKER] Gateway ngắt kết nối:', c.id); });

    return {
        aedes,
        serverAuth: { username: 'server', password: serverPassword },
        // Luồng nội bộ cho mqtt.js của server (không qua mạng)
        localStream() { const [a, b] = duplexPair(); aedes.handle(b); return a; },
        // Nhận WebSocket đã nâng cấp từ HTTP server
        handleWebSocket(ws) { aedes.handle(createWebSocketStream(ws)); },
        close(cb) { aedes.close(cb); }
    };
}

module.exports = { createEmbeddedBroker, _match: match, _filterAllowed: filterAllowed };
