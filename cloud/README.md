# ☁️ Tầng Cloud (Stage 4) — Server & Web app tự xây dựng

```
ESP32 node ──WiFi/MQTT──▶ GATEWAY (laptop: Mosquitto + Node-RED Edge)
                               │ MQTT bridge (chỉ dữ liệu đã xử lý)   ▲ lệnh flood/cloud/#
                               ▼                                      │
                         CLOUD VM: Mosquitto (mật khẩu + ACL) ──▶ server.js ──▶ SQLite
                                                                  │
                                         HTTPS (Caddy) ◀── REST API + WebSocket
                                              │
                         ┌────────────────────┴────────────────────┐
                    Trang công khai /                        Console quản lý /admin
               (người dân: bản đồ ngập)        (đơn vị vận hành: xác nhận, lịch sử, thống kê,
                                                  cấu hình & hiệu chuẩn node từ xa)
```

| Thành phần | File | Vai trò |
|---|---|---|
| Broker cloud | `mosquitto/mosquitto.conf`, `mosquitto/acl` | Chỉ nhận kết nối có mật khẩu; ACL giới hạn topic từng tài khoản |
| Server | `server/server.js` | Nhận MQTT → lưu SQLite (đo 5 s/lần, nhật ký, sự cố) → REST API + WebSocket; gửi lệnh xuống gateway |
| Web công khai | `server/public/index.html` | Bản đồ (Leaflet + OpenStreetMap), mức ngập, lời khuyên cho người đi đường |
| Web quản lý | `server/public/admin.html` | Yêu cầu vận hành + nút xác nhận, KPI 7 ngày, biểu đồ lịch sử, bảng thiết bị, cấu hình/hiệu chuẩn từ xa, nhật ký |
| HTTPS | `Caddyfile` | Tự xin chứng chỉ Let's Encrypt |

## API

| Phương thức | Đường dẫn | Quyền | Mô tả |
|---|---|---|---|
| GET | `/api/state` | công khai | Trạng thái Edge, gateway, heartbeat thiết bị (ẩn IP), dự báo AI |
| GET | `/api/history?node=cong1&hours=6` | công khai | Lịch sử đo (tối đa 72 giờ) |
| POST | `/api/login` | — | Kiểm tra mã quản trị |
| GET | `/api/events`, `/api/incidents` | quản trị | Nhật ký; sự cố + thống kê (số sự cố, thời gian phản hồi, thời gian báo trước) |
| POST | `/api/ack` | quản trị | Xác nhận đã xử lý → `flood/cloud/ack` |
| POST | `/api/nodes/:id/config` | quản trị | Cấu hình → `flood/cloud/config/<id>` |
| POST | `/api/nodes/:id/cmd` | quản trị | `{"cmd":"calibrate"\|"report"}` → `flood/cloud/cmd/<id>` |
| POST | `/api/nodes/:id/alarm` | quản trị | `{"action":"off"\|"on"}` → `flood/cloud/alarm/<id>` |
| WS | `/ws` | công khai / quản trị | Đẩy `state`, `gateway`, `info`, `ai`; gửi `{"auth":token}` để nhận thêm `log`, `incident` |

Quyền quản trị: header `x-admin-token: <ADMIN_TOKEN>`.

## Triển khai lên máy ảo (≈ 30 phút)

**1. Tạo VM miễn phí** — một trong hai:
- Oracle Cloud Free Tier: VM.Standard.E2.1.Micro hoặc Ampere A1, Ubuntu 22.04/24.04
- Google Cloud: e2-micro (us-west1 / us-central1 / us-east1 miễn phí), Ubuntu

Mở cổng vào (firewall / security list): **22, 80, 443, 1883**.
Oracle Ubuntu còn chặn bằng iptables trong máy — chạy thêm:
```bash
sudo iptables -I INPUT -p tcp -m multiport --dports 80,443,1883 -j ACCEPT && sudo netfilter-persistent save
```

**2. Cài Docker và chép mã nguồn**
```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER && newgrp docker
git clone <repo của nhóm> iot && cd iot/cloud      # hoặc scp thư mục cloud/ lên
cp .env.example .env && nano .env                  # đổi DOMAIN = <IP>.sslip.io và 3 mật khẩu
docker compose up -d --build
docker compose logs -f server                      # thấy "[MQTT] Đã kết nối" là được
```
Mở `https://<IP>.sslip.io` (trang công khai) và `https://<IP>.sslip.io/admin` (đăng nhập bằng `ADMIN_TOKEN`).

**3. Nối gateway (laptop) lên cloud**
```bat
copy mosquitto\conf.d\cloud-bridge.conf.example mosquitto\conf.d\cloud-bridge.conf
:: sửa address = <IP VM>:1883, remote_password = EDGE_PASSWORD trong .env
start_all.bat
```
Kiểm tra: console quản lý hiện **Gateway online**; tắt Mosquitto trên laptop → sau ≤ 60 s hiện **mất kết nối**.

## Chạy thử trên máy cá nhân (không cần VM)

```bash
cd cloud/server && npm install
# Windows PowerShell:  $env:ADMIN_TOKEN="test"; $env:PORT="8080"; npm start
ADMIN_TOKEN=test npm start            # kết nối mqtt://localhost:1883 (Mosquitto của gateway)
```
Mở http://localhost:8080 và http://localhost:8080/admin. Khi chạy chung một broker với gateway thì không cần bridge.
Yêu cầu Node.js ≥ 22.5 (dùng SQLite tích hợp `node:sqlite`).

## Giới hạn (nêu trung thực trong báo cáo)
- Bridge MQTT cổng 1883 có mật khẩu + ACL nhưng **chưa mã hoá TLS**; web đã qua HTTPS. Bước tiếp theo: bridge qua 8883 với TLS.
- Một mã quản trị dùng chung, chưa có tài khoản riêng từng người.
- Vị trí trên bản đồ là minh hoạ.
