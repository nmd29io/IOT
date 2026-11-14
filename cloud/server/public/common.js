// Hàm dùng chung cho trang công khai và console quản lý.
const LEVEL = {
    green:  { cls: 'green',  text: 'An toàn' },
    yellow: { cls: 'yellow', text: 'Cảnh báo' },
    red:    { cls: 'red',    text: 'Nguy hiểm' }
};
const COLOR = lv => getComputedStyle(document.documentElement).getPropertyValue('--' + ({ green: 'green', yellow: 'yellow', red: 'red' }[lv] || 'gray')).trim();

const store = {
    get(k) { try { return localStorage.getItem(k); } catch { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch { /* chế độ riêng tư */ } },
    del(k) { try { localStorage.removeItem(k); } catch { /* bỏ qua */ } }
};

async function api(path, { method = 'GET', body, token } = {}) {
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers['x-admin-token'] = token;
    const res = await fetch(path, { method, headers, body: body ? JSON.stringify(body) : undefined });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw Object.assign(new Error(data.error || res.statusText), { status: res.status });
    return data;
}

// WebSocket tự kết nối lại; nếu không được thì trang vẫn tự làm mới bằng polling.
function connectWS(onMessage, token) {
    let ws, timer;
    const open = () => {
        ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
        ws.onopen = () => { if (token) ws.send(JSON.stringify({ auth: token })); };
        ws.onmessage = e => { try { const m = JSON.parse(e.data); onMessage(m.type, m.data); } catch { /* bỏ qua */ } };
        ws.onclose = () => { clearTimeout(timer); timer = setTimeout(open, 3000); };
    };
    open();
}

const pad = n => String(n).padStart(2, '0');
function fmtTime(ts) {
    if (!ts) return '—';
    const d = new Date(ts);
    return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}
function fmtDateTime(ts) {
    if (!ts) return '—';
    const d = new Date(ts);
    return `${pad(d.getDate())}/${pad(d.getMonth() + 1)} ${fmtTime(ts)}`;
}
function ago(ts) {
    if (!ts) return '—';
    const s = Math.max(0, Math.round((Date.now() - ts) / 1000));
    if (s < 60) return s + ' giây trước';
    if (s < 3600) return Math.round(s / 60) + ' phút trước';
    return Math.round(s / 3600) + ' giờ trước';
}
function dur(s) {
    if (s === null || s === undefined) return '—';
    s = Math.round(s);
    return s < 60 ? s + ' giây' : Math.floor(s / 60) + ' phút ' + (s % 60) + ' giây';
}
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const nodeName = (id, meta) => (meta && meta[id] && meta[id].name) || id;
const isSimulated = info => Object.values(info || {}).some(i => String(i.fw || '').startsWith('sim'));

function pill(level, text) {
    const l = LEVEL[level];
    return `<span class="pill ${l ? l.cls : ''}">${esc(text || (l ? l.text : 'Chưa có dữ liệu'))}</span>`;
}

function reading(n) {
    if (!n || !n.data) return 'Chưa có dữ liệu';
    const d = n.data;
    if (n.kind === 'drain') return `${d.L}% đầy · ${d.m} cm · ${d.v >= 0 ? '+' : ''}${d.v} cm/phút`;
    return `Ngập ${d.h} cm`;
}

function adviceFor(n) {
    if (!n || !n.level) return '';
    if (n.kind === 'road') return { green: 'Đi lại bình thường.', yellow: 'Xe máy đi chậm, cẩn trọng.', red: 'Không nên đi xe máy qua đoạn này.' }[n.level];
    return { green: 'Cống thoát nước bình thường.', yellow: 'Mực nước trong cống đang cao – đơn vị quản lý đang theo dõi.', red: 'Cống sắp quá tải – nguy cơ ngập đường. Hạn chế đi qua.' }[n.level];
}

function toast(msg) {
    let el = document.getElementById('toast');
    if (!el) { el = document.createElement('div'); el.id = 'toast'; el.className = 'toast'; document.body.appendChild(el); }
    el.textContent = msg;
    el.classList.add('show');
    clearTimeout(el._t);
    el._t = setTimeout(() => el.classList.remove('show'), 3500);
}
