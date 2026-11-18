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
| App quản lý (PWA) | `server/public/admin.html`, `manifest.webmanifest`, `sw.js`, `icons/` | Cài lên màn hình chính điện thoại. Yêu cầu vận hành + nút xác nhận, cảnh báo gần đây, KPI 7 ngày, biểu đồ lịch sử, bảng thiết bị, cấu hình/hiệu chuẩn từ xa, nhật ký |
| Thông báo đẩy | `server.js` (thư viện `web-push`, chuẩn VAPID) + `sw.js` | Cảnh báo từ gateway (`flood/edge/alert`) → điện thoại rung kể cả khi app đóng; bấm **✅ Đã xử lý** ngay trên thông báo |
| HTTPS | `Caddyfile` | Tự xin chứng chỉ Let's Encrypt |

## API

| Phương thức | Đường dẫn | Quyền | Mô tả |
|---|---|---|---|
| GET | `/api/state` | công khai | Trạng thái Edge, gateway, heartbeat thiết bị (ẩn IP), dự báo AI |
| GET | `/api/history?node=cong1&hours=6` | công khai | Lịch sử đo (tối đa 72 giờ) |
| POST | `/api/login` | — | Kiểm tra mã quản trị |
| GET | `/api/events`, `/api/incidents` | quản trị | Nhật ký; sự cố + thống kê (số sự cố, thời gian phản hồi, thời gian báo trước) |
| POST | `/api/ack` | quản trị | Xác nhận đã xử lý → `flood/cloud/ack` |
| GET | `/api/alerts` | quản trị | Cảnh báo gần đây |
| GET | `/api/push/key` | công khai | Khoá công khai VAPID để trình duyệt đăng ký |
| POST | `/api/push/subscribe`, `/api/push/unsubscribe`, `/api/push/test` | quản trị | Bật / tắt / thử thông báo trên một thiết bị |
| POST | `/api/push/ack` | thiết bị đã đăng ký | Nút "Đã xử lý" trên thông báo (xác thực bằng đăng ký push, không cần mã) |
| POST | `/api/nodes/:id/config` | quản trị | Cấu hình → `flood/cloud/config/<id>` |
| POST | `/api/nodes/:id/cmd` | quản trị | `{"cmd":"calibrate"\|"report"}` → `flood/cloud/cmd/<id>` |
| POST | `/api/nodes/:id/alarm` | quản trị | `{"action":"off"\|"on"}` → `flood/cloud/alarm/<id>` |
| WS | `/ws` | công khai / quản trị | Đẩy `state`, `gateway`, `info`, `ai`; gửi `{"auth":token}` để nhận thêm `log`, `incident`, `alert` |

Quyền quản trị: header `x-admin-token: <ADMIN_TOKEN>`.

## Các cách triển khai

| | **A. Render.com** (cách chính) | **B. Máy ảo / VPS** | **C. Cloudflare Tunnel** (dự phòng) |
|---|---|---|---|
| Chi phí, thẻ | Gói free, không cần thẻ (theo trang Render) | Cần thẻ quốc tế hoặc thuê VPS | Miễn phí, không tài khoản |
| Server chạy ở | Cloud (Render, Singapore) | Cloud (VM) | **Laptop của nhóm** |
| Broker MQTT trên cloud | Tích hợp trong server (`broker.js`, aedes), `BROKER=embedded` | Mosquitto riêng (`docker-compose.yml`) | Tích hợp (như A) |
| Gateway nối lên bằng | `gateway/cloud_link` — MQTT qua **wss://…/mqtt** (cổng 443, TLS) | Mosquitto bridge, cổng 1883 | `cloud_link` (ws trong LAN hoặc wss qua tunnel) |
| Phân quyền | Tài khoản `edge` + ACL trong `broker.js` (giống file `acl`) | Mật khẩu + file `acl` | Như A |
| Địa chỉ web | Cố định `https://<tên>.onrender.com` | Cố định | **Đổi mỗi lần chạy** |
| Dữ liệu | Mất khi service khởi động lại | Lưu lâu dài | Lưu trên laptop |

Cả ba dùng chung một `server.js` và web app; cloud không bao giờ ra lệnh thẳng cho node mà chỉ gửi vào `flood/cloud/#`
để gateway kiểm duyệt. **Không có mật khẩu nào nằm trong repo**: A khai trong *Environment* của Render,
B trong `cloud/.env`, C trong `cloud/tunnel.env` (cả hai đều bị `.gitignore`).

## A. Triển khai lên Render.com (≈ 15 phút)

```
ESP32 ─MQTT─▶ Laptop: Mosquitto + Node-RED + cloud_link.js ──wss://<tên>.onrender.com/mqtt──▶ Render: server.js
                                                                                              (broker tích hợp + SQLite + web/PWA)
                                                                                                   ▲ điện thoại (4G)
```
Render build từ GitHub theo `render.yaml` (gốc repo) và `cloud/server/Dockerfile.embedded`.

**0. Chuẩn bị mật khẩu** (PowerShell, chạy trên máy, không gửi lên nhóm chat):
```powershell
-join ((48..57)+(65..90)+(97..122) | Get-Random -Count 24 | % {[char]$_})      # chạy 2 lần: ADMIN_TOKEN, EDGE_PASSWORD
cd cloud\server; node -e "console.log(require('web-push').generateVAPIDKeys())"   # VAPID_PUBLIC_KEY, VAPID_PRIVATE_KEY
```

**1. Tạo service**
- Đăng ký render.com (đăng nhập bằng GitHub). Nếu bị đòi thẻ → dùng cách C.
- Đã push code lên GitHub → **New + → Blueprint** → chọn repo, nhánh `feature/iot-platform-v2` → Render đọc `render.yaml`
  và hỏi 4 biến `ADMIN_TOKEN`, `EDGE_PASSWORD`, `VAPID_PUBLIC_KEY`, `VAPID_PRIVATE_KEY` → điền → **Apply**.
- Hoặc tạo tay: **New + → Web Service** → repo → *Language* **Docker**, *Instance type* **Free**,
  *Dockerfile Path* `./cloud/server/Dockerfile.embedded`, *Docker Build Context* `./cloud/server`,
  *Health Check Path* `/healthz`, mục **Environment** thêm 4 biến trên và `BROKER=embedded`.
- Chờ build (3–5 phút). Mở `https://<tên>.onrender.com/healthz` → thấy `"broker":"embedded"` là được.
- Repo private: Render cần được cấp quyền đọc repo (chủ repo cài Render GitHub App), hoặc dùng repo/fork public.

**2. Nối gateway (laptop) lên Render**
```bat
copy gateway\cloud_link\config.example.json gateway\cloud_link\config.json
:: sửa "url": "wss://<tên>.onrender.com/mqtt", "password": <EDGE_PASSWORD>
start_all.bat
```
Cửa sổ `cloud_link` hiện `[CLOUD] Đã kết nối`. Trang quản trị hiện **Gateway online**; đóng cửa sổ `cloud_link`
→ trang quản trị báo **mất kết nối** (Last Will).

**3. Điện thoại:** mở `https://<tên>.onrender.com/admin` → đăng nhập → **📲 Cài ứng dụng** → **🔔 Bật thông báo** →
**Gửi thử**. Mỗi lần mở app, thiết bị tự đăng ký lại với server, nên service khởi động lại cũng không phải bật lại thông báo.

**Lưu ý:** gói free **ngủ sau 15 phút không có truy cập** (đang có gateway gửi qua WebSocket thì không ngủ). Trước giờ demo
mở trang một lần (thức dậy ≈ 1 phút) rồi chạy `start_all.bat`; chạy trước khoảng 30 phút để có biểu đồ lịch sử
(dữ liệu bắt đầu lại sau mỗi lần service khởi động lại). Sửa code server → push lên GitHub, Render tự build lại.

## B. Triển khai lên máy ảo (≈ 30 phút)

**1. Tạo VM miễn phí** — một trong các cách:
- **Azure for Students** (không cần thẻ, nhưng email VNU của nhóm đã bị từ chối — thử email thành viên khác): đăng ký tại azure.microsoft.com/free/students
  bằng email trường (`@vnu.edu.vn`), nhận 100 USD dùng 12 tháng. Tạo *Virtual machine* → Ubuntu 24.04,
  cỡ **B1s** hoặc **B2ats v2**, xác thực bằng SSH key hoặc mật khẩu, mục *Networking* mở cổng 22, 80, 443 (cổng 1883
  thêm sau ở *Network settings → Create port rule → Inbound*).
- Oracle Cloud Free Tier / Google Cloud e2-micro: cần thẻ Visa/Master quốc tế để xác minh (thẻ ATM nội địa thường bị từ chối).
- VPS giá rẻ trong nước (~50–100 nghìn đ/tháng, 1 vCPU 1 GB RAM, Ubuntu) — làm theo các bước dưới y hệt.

Mở cổng vào (firewall / security list / network security group): **22, 80, 443, 1883**.
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

**Cài app lên điện thoại người quản lý:** mở `https://<IP>.sslip.io/admin` trên điện thoại → đăng nhập →
**📲 Cài ứng dụng** (Android/Chrome) hoặc Safari → Chia sẻ → *Thêm vào MH chính* (iPhone, iOS 16.4+) →
mở app → **🔔 Bật thông báo** → **Gửi thử**. Thông báo đẩy bắt buộc chạy qua HTTPS (Caddy đã lo phần này).
Khoá VAPID được tạo tự động lần đầu và lưu trong cơ sở dữ liệu.

**3. Nối gateway (laptop) lên cloud**
```bat
copy mosquitto\conf.d\cloud-bridge.conf.example mosquitto\conf.d\cloud-bridge.conf
:: sửa address = <IP VM>:1883, remote_password = EDGE_PASSWORD trong .env
start_all.bat
```
Kiểm tra: console quản lý hiện **Gateway online**; tắt Mosquitto trên laptop → sau ≤ 60 s hiện **mất kết nối**.

## C. Dự phòng: Cloudflare Tunnel (server chạy trên laptop)

Dùng khi Render đòi thẻ hoặc gặp sự cố đúng hôm demo. Khi trình bày nói đúng: *"server tự host, truy cập từ xa qua
Cloudflare Tunnel"* — không gọi là triển khai cloud.

```bat
start_cloud_tunnel.bat            :: laptop B làm "cloud" (khuyên dùng: gateway và server tách hai máy)
start_cloud_tunnel.bat -Local     :: gateway và server cùng một laptop
```
Script tự cài `cloudflared` (winget) nếu chưa có, tự tạo mật khẩu vào `cloud\tunnel.env`, chạy server ở chế độ broker
tích hợp, mở tunnel rồi in ra:
- địa chỉ `https://<ngẫu-nhiên>.trycloudflare.com` (và `/admin`) cho điện thoại;
- địa chỉ cho `gateway\cloud_link\config.json` của laptop gateway: `ws://<IP laptop B>:8080/mqtt` khi cùng hotspot
  (ổn định hơn), hoặc `wss://<ngẫu-nhiên>.trycloudflare.com/mqtt` qua Internet; `password` = `EDGE_PASSWORD` trong `cloud\tunnel.env`.

Địa chỉ tunnel **đổi mỗi lần chạy** → phải cài lại app và bật lại thông báo trên điện thoại.

## Chạy thử trên máy cá nhân (không cần VM)

```bash
cd cloud/server && npm install
# Windows PowerShell:  $env:ADMIN_TOKEN="test"; $env:PORT="8080"; npm start
ADMIN_TOKEN=test npm start            # kết nối mqtt://localhost:1883 (Mosquitto của gateway)
```
Mở http://localhost:8080 và http://localhost:8080/admin. Khi chạy chung một broker với gateway thì không cần bridge.
Yêu cầu Node.js ≥ 22.5 (dùng SQLite tích hợp `node:sqlite`).

## Giới hạn (nêu trung thực trong báo cáo)
- Cách A (Render free): dữ liệu lưu trong `/tmp`, mất khi service khởi động lại; service ngủ khi 15 phút không có truy cập.
  Bước tiếp theo: gói trả phí có ổ đĩa / VPS, hoặc cơ sở dữ liệu cloud.
- Cách B: bridge MQTT cổng 1883 có mật khẩu + ACL nhưng **chưa mã hoá TLS** (cách A đi qua wss/TLS); web đã qua HTTPS.
- Một mã quản trị dùng chung, chưa có tài khoản riêng từng người.
- Vị trí trên bản đồ là minh hoạ.
