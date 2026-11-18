// Service worker của app quản lý (PWA): chạy nền trên điện thoại, nhận Web Push từ server của nhóm.
const CACHE = 'flood-admin-v1';
const SHELL = ['/admin.html', '/style.css', '/common.js', '/manifest.webmanifest', '/icons/icon-192.png', '/icons/icon-512.png'];

self.addEventListener('install', e => {
    e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener('activate', e => {
    e.waitUntil(caches.keys()
        .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
        .then(() => self.clients.claim()));
});

// Mạng trước, mất mạng thì dùng bản đã lưu (không cache API / WebSocket / thư viện ngoài)
self.addEventListener('fetch', e => {
    const url = new URL(e.request.url);
    if (e.request.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/api/') || url.pathname === '/ws') return;
    e.respondWith(
        fetch(e.request)
            .then(res => { const copy = res.clone(); caches.open(CACHE).then(c => c.put(e.request, copy)); return res; })
            .catch(() => caches.match(e.request))
    );
});

// Nhận cảnh báo đẩy từ server
self.addEventListener('push', e => {
    let a = {};
    try { a = e.data ? e.data.json() : {}; } catch { a = { title: 'Cảnh báo ngập', body: e.data && e.data.text() }; }
    const critical = a.severity === 'critical';
    const options = {
        body: a.body || '',
        icon: '/icons/icon-192.png',
        badge: '/icons/icon-192.png',
        tag: a.key || a.node || 'flood',
        renotify: true,
        requireInteraction: critical,              // cảnh báo khẩn không tự biến mất
        vibrate: critical ? [300, 120, 300, 120, 300] : [150],
        data: { key: a.key || null, url: a.url || '/admin.html' },
        actions: a.key ? [{ action: 'ack', title: '✅ Đã xử lý' }, { action: 'open', title: 'Mở' }] : []
    };
    e.waitUntil(self.registration.showNotification(a.title || 'Cảnh báo ngập', options));
});

self.addEventListener('notificationclick', e => {
    e.notification.close();
    const { key, url } = e.notification.data || {};
    if (e.action === 'ack' && key) {
        e.waitUntil((async () => {
            const sub = await self.registration.pushManager.getSubscription();
            const res = await fetch('/api/push/ack', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ endpoint: sub && sub.endpoint, key })
            }).catch(() => null);
            await self.registration.showNotification(res && res.ok ? '✅ Đã gửi xác nhận' : '⚠️ Chưa gửi được xác nhận',
                { body: res && res.ok ? 'Gateway sẽ ghi nhận người xác nhận và thời gian phản hồi.' : 'Mở ứng dụng để xác nhận.',
                  icon: '/icons/icon-192.png', tag: 'ack-result' });
        })());
        return;
    }
    e.waitUntil((async () => {
        const all = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
        const win = all.find(c => new URL(c.url).pathname === '/admin.html');
        if (win) return win.focus();
        return self.clients.openWindow(url || '/admin.html');
    })());
});
