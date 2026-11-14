# Hướng dẫn bảo vệ đề tài — đối chiếu tiêu chí, giải thích hệ thống, kịch bản demo

Tài liệu nội bộ cho Nhóm 1. Mục tiêu: mỗi thành viên giải thích được **toàn bộ** hệ thống, không chỉ phần mình làm.

---

## 1. Đối chiếu tiêu chí chấm điểm

| Tiêu chí | Điểm | Hệ thống đáp ứng bằng | Chỉ ra khi demo |
|---|---|---|---|
| **Ý tưởng IoT** (không phải nhúng/tự động hoá) | 1 | Giá trị đến từ **dữ liệu nhiều node được gom và kết hợp**: cống báo trước khi đường ngập (đo được *thời gian báo trước*), dữ liệu lên cloud phục vụ 2 nhóm người dùng, quản lý thiết bị từ xa. Hệ thống **không** tự bật bơm — con người ra quyết định và xác nhận | Tin "Ngập đang lan rộng – cống đã báo trước X phút"; KPI trên console |
| **Kiến trúc IoT** (node, gateway, cloud) | 2 | **Node**: 2 ESP32 + JSN-SR04T (+ đèn/còi). **Gateway**: laptop chạy Mosquitto + Node-RED (Edge IT). **Cloud**: VM chạy Mosquitto + server + SQLite + web, nối với gateway qua **MQTT bridge** | Sơ đồ mục 2; rút điện gateway → cloud báo "Gateway mất kết nối" |
| **Tự xây dựng IoT Platform** (node/gateway, server, thiết bị người dùng) | 1 | Firmware tự viết (`firmware/flood_node`), logic Edge tự viết (function node Node-RED), **server tự viết** (`cloud/server/server.js`: MQTT→SQLite→REST/WebSocket), **web app / mobile app tự viết**: trang công khai `public/index.html` và **app quản lý PWA** `public/admin.html` cài lên điện thoại, nhận **Web Push** do server của nhóm gửi | Cài app lên điện thoại trước mặt giám khảo, cho xem thông báo đẩy đến máy |
| **Độ hoàn thiện** | 1 | Chạy đủ kịch bản: an toàn → cảnh báo → khẩn cấp (yêu cầu + xác nhận) → hồi phục; mất mạng; cấu hình/hiệu chuẩn từ xa | Mục 5 |
| **AI/ML hoặc triển khai cloud** | 1 | **Triển khai trên cloud** (VM thật, HTTPS) — chắc chắn đạt. AI dự báo là phần bổ sung, trình bày đúng mức (mục 7) | URL `https://<IP>.sslip.io` |
| Video 5–7 phút | (−0,5 nếu thiếu) | Kịch bản mục 4 | — |
| Báo cáo / slide | 2 + 2 | Dàn ý mục 8 | — |

---

## 2. Giải thích kiến trúc (phải nói được trong 1 phút)

```
 Stage 1–2 (hiện trường)        │ Stage 3 (Edge IT)                │ Stage 4 (Cloud)
                                │                                  │
 [Node cống] JSN-SR04T → ESP32 ─┼─WiFi/MQTT─▶ Mosquitto ◀─▶ Node-RED ─bridge─▶ Mosquitto ─▶ Server ─▶ SQLite
 [Node đường] JSN-SR04T → ESP32 ┤             (laptop)    │         │ (VM)                  │
      └─ đèn + còi (tự chạy     │                         └─▶ Dashboard nội bộ              ├─▶ Web công khai (người dân)
         khi mất mạng)          │                                                           └─▶ App quản lý PWA + Web Push (điện thoại người quản lý)
                                │                                  │
 ◀──────── luồng điều khiển: tắt còi, cấu hình, hiệu chuẩn, xác nhận (App/Web/Dashboard → cloud → gateway kiểm duyệt → node)
```

**Ai làm gì (phân vai xử lý):**
- **Node cống**: chỉ đo, lọc trung vị 7 mẫu, gửi `m` và `L`. Không tự quyết định.
- **Node đường**: đo, **tự** phân mức (hysteresis + giữ ổn định 20 s) và bật đèn/còi — vì người đi đường cần cảnh báo kể cả khi mất mạng (*fail-safe tại biên*).
- **Gateway / Edge (Node-RED)**: trung bình trượt, **tốc độ dâng theo cửa sổ 1 phút**, phân mức cống, **kết hợp 2 node**, quản lý sự cố (yêu cầu → nhắc lại sau 10 phút → xác nhận → đóng), watchdog thiết bị, kiểm duyệt mọi lệnh đi xuống node.
- **Cloud**: lưu trữ lâu dài, thống kê, phục vụ người dùng ở xa qua Internet. Cloud **không** điều khiển node trực tiếp — lệnh đi qua `flood/cloud/#` và gateway kiểm tra trước.

**Vì sao chỉ đẩy dữ liệu đã xử lý lên cloud?** Node đo 2 s/lần; gateway gom, tính toán rồi chỉ đẩy trạng thái + sự kiện → giảm băng thông, cloud vẫn có đủ dữ liệu (lưu 5 s/mẫu). Đây là lý do có tầng Edge.

**Đối chiếu 7 tầng IoT World Forum:**
1 Thiết bị: ESP32, JSN-SR04T, đèn, còi · 2 Kết nối: WiFi, MQTT, bridge · 3 Edge: Node-RED · 4 Lưu trữ: SQLite trên cloud của nhóm · 5 Trừu tượng dữ liệu: JSON + cấu trúc topic `flood/<id>/...` · 6 Ứng dụng: web công khai, app quản lý (PWA), dashboard · 7 Cộng tác & quy trình: thông báo đẩy yêu cầu vận hành → bấm "Đã xử lý" trên thông báo → nhắc lại sau 10 phút → thống kê thời gian phản hồi.

**Security (yếu tố xuyên suốt):** broker cloud có mật khẩu + ACL theo topic; web qua HTTPS; mã quản trị cho console; nút "Đã xử lý" trên thông báo chỉ hợp lệ với thiết bị quản trị đã đăng ký; secrets không đưa lên git. Hạn chế: bridge chưa TLS (nêu trong báo cáo).

---

## 3. "Vì sao đây là IoT, không phải hệ nhúng / tự động hoá?"

| | Hệ nhúng | Tự động hoá | **Hệ thống của nhóm** |
|---|---|---|---|
| Phạm vi | 1 thiết bị | 1 vòng điều khiển kín | **Nhiều node + gateway + cloud + nhiều người dùng** |
| Quyết định | tại chip | bộ điều khiển tự bật bơm | **dữ liệu kết hợp 2 điểm → người quản lý quyết định và xác nhận** |
| Giá trị | đèn sáng khi ngập | bơm tự chạy | **báo trước X phút, thống kê phản hồi, ai ở đâu cũng xem được** |
| Quản lý | nạp lại firmware | — | **cấu hình, hiệu chuẩn, giám sát thiết bị từ xa** |

Câu trả lời mẫu: *"Nếu bỏ mạng đi, mỗi node chỉ còn là một mạch đo. Giá trị của hệ thống nằm ở chỗ dữ liệu hai điểm được gom lại: cống báo đỏ trước khi đường ngập, hệ thống đo được báo trước bao nhiêu phút, gửi yêu cầu cho người phụ trách, theo dõi họ phản hồi sau bao lâu, và người dân ở bất kỳ đâu cũng xem được bản đồ. Đèn/còi tại chỗ chỉ là cơ chế dự phòng khi mất mạng."*

---

## 4. Kịch bản video (5–7 phút)

| Thời gian | Nội dung | Hình ảnh |
|---|---|---|
| 0:00–0:40 | Bài toán: Hà Nội ngập, ứng phó bị động, thiếu dữ liệu tại điểm | Ảnh ngập, số liệu 20% quy hoạch |
| 0:40–1:30 | Giải pháp + kiến trúc 4 tầng (sơ đồ mục 2), chỉ từng thiết bị trên mô hình fomex | Sơ đồ, quay mô hình |
| 1:30–2:30 | **Kịch bản 1 – nước dâng**: đổ nước vào ống → console: VÀNG do *tốc độ dâng* → ĐỎ → **điện thoại rung, thông báo "YÊU CẦU bật bơm/mở van"** | Chia đôi màn hình: mô hình + điện thoại |
| 2:30–3:15 | **Kịch bản 2 – tràn ra đường**: nước sang khay → đèn vàng/đỏ, còi → tin "Ngập đang lan rộng, cống báo trước X phút" | Quay đèn, thông báo trên điện thoại |
| 3:15–3:50 | **Kịch bản 3 – vận hành**: bấm **✅ Đã xử lý ngay trên thông báo** → console cập nhật "đã xác nhận", tắt còi từ web | Điện thoại + console |
| 3:50–4:30 | **Kịch bản 4 – chống báo sai & mất mạng**: đưa tay qua cảm biến đường → không đổi mức; rút WiFi → đèn vẫn chạy, cloud báo node offline | Quay đèn + console |
| 4:30–5:15 | **Kịch bản 5 – quản lý từ xa**: đổi ngưỡng / hiệu chuẩn d0 từ web trên điện thoại 4G → nhật ký "duong1: đã áp dụng cấu hình" | Điện thoại |
| 5:15–6:15 | Kết quả đo: bảng sai số, độ trễ, thời gian báo trước, KPI 7 ngày; trang công khai bản đồ | Bảng số liệu |
| 6:15–7:00 | Đánh giá, hạn chế, hướng phát triển | Slide |

Quay màn hình console và quay mô hình **cùng lúc** (2 thiết bị), ghép cạnh nhau để thấy độ trễ thật.

---

## 5. Checklist trước giờ demo

- [ ] VM cloud chạy, mở được `https://<IP>.sslip.io` bằng 4G (không dùng WiFi trường)
- [ ] Laptop + 2 ESP32 dùng **hotspot điện thoại**; laptop có IP cố định trong `config.h`
- [ ] `start_all.bat` → dashboard nội bộ OK; console cloud hiện **Gateway online**
- [ ] App quản lý đã cài trên điện thoại, đã **Bật thông báo**, bấm **Gửi thử** nhận được
- [ ] Ngưỡng đường đã quy đổi cho khay (VD 2/4/3/1,5 cm) qua web; hiệu chuẩn d0 khi khay khô, H khi ống rỗng
- [ ] Nước, cốc, khăn; pin dự phòng cho điện thoại
- [ ] Phương án B: `python test_simulator.py` nếu phần cứng hỏng (nói rõ là mô phỏng)
- [ ] Xoá dữ liệu thử: `docker compose down -v` rồi `up -d` (chỉ khi muốn KPI sạch)

---

## 6. Câu hỏi giám khảo hay hỏi

| Câu hỏi | Trả lời ngắn |
|---|---|
| App trên điện thoại là app gì? | App của nhóm: web app dạng **PWA**, cài lên màn hình chính như app thật. Server của nhóm gửi thông báo đẩy theo chuẩn **Web Push (VAPID)** qua kênh push có sẵn của trình duyệt; người quản lý bấm "Đã xử lý" ngay trên thông báo. Không dùng Telegram hay dịch vụ IoT bên thứ ba. |
| Gateway ở đâu? | Laptop chạy Mosquitto + Node-RED. Nó gom dữ liệu từ các node trong mạng cục bộ, xử lý tại biên và là điểm duy nhất nối lên cloud qua MQTT bridge. Thực tế thay bằng Raspberry Pi / máy tính công nghiệp tại trạm bơm. |
| Sao không gửi thẳng ESP32 lên cloud? | Được, nhưng mất lợi ích Edge: tính toán gần nguồn, vẫn cảnh báo khi mất Internet, giảm dữ liệu lên cloud, và một điểm kiểm duyệt lệnh xuống node. |
| Platform nào nhóm tự viết? | Firmware, toàn bộ logic Edge, server cloud (MQTT→SQLite→API→WebSocket) và 2 web app. Node-RED/Mosquitto/SQLite chỉ là runtime, như Linux hay Express. |
| Mất WiFi thì sao? | Node đường vẫn tự đổi đèn/còi (task FreeRTOS riêng). Broker phát Last Will → gateway báo offline (thông báo đẩy + web). Gateway mất Internet → cloud nhận Last Will của bridge → web báo "gateway mất kết nối". |
| Xe chạy qua cảm biến đường? | Mức mới phải giữ ổn định 20 s mới được công nhận; xe qua chỉ vài giây → bị bỏ. Demo bằng tay. |
| Tốc độ dâng tính thế nào? | Trung bình trượt 5 mẫu, so với giá trị cách đây 1 phút: v = (m_tb − m_tb_1phút) / Δt. Cống 60% nhưng dâng 5 cm/phút → ĐỎ sớm hơn chờ 80%. |
| Hysteresis để làm gì? | Lên ĐỎ khi h > 20, chỉ về VÀNG khi h < 15 → mực nước dao động quanh 20 cm không làm đèn nhấp nháy, không spam tin. |
| Sao không tự bật bơm? | Bơm/van thuộc đơn vị quản lý, ảnh hưởng cả lưu vực; hệ thống cung cấp dữ liệu kịp thời và quy trình xác nhận có trách nhiệm (ai xác nhận, sau bao lâu). Tự động hoá vòng kín là hướng phát triển. |
| Sao chỉ có 1 ESP32 mà nói 2 node? | Do giới hạn phần cứng, một ESP32 gắn 2 cảm biến và chạy **2 node logic**: mỗi node có kết nối MQTT, Last Will, cấu hình và hiệu chuẩn riêng. Phía gateway/cloud không phân biệt — thêm mạch thứ hai chỉ cần đổi `NODE_MODE`, không sửa server. Ngoài thực địa mỗi điểm đo là một mạch riêng. (Nói thẳng điều này, đừng giấu.) |
| Bảo mật? | Mật khẩu + ACL trên broker cloud, HTTPS, mã quản trị, chỉ thiết bị quản trị đã đăng ký mới xác nhận được từ thông báo; mật khẩu ngoài git. Chưa TLS cho bridge – sẽ dùng cổng 8883. |
| Thêm node mới? | Nạp firmware với mã node mới (`DRAIN_ID`/`ROAD_ID`); flow subscribe `flood/+/...` nên tự nhận, node tự hiện trong bảng thiết bị và trên web (thêm toạ độ vào `nodes.json`). |
| Dữ liệu dùng để làm gì về lâu dài? | Thống kê số sự cố, thời gian báo trước, thời gian phản hồi theo từng điểm → biết điểm nào cần nâng cấp cống trước (đầu tư đúng chỗ). |

---

## 7. Về phần AI — trình bày đúng mức

- Tiêu chí "AI/ML **hoặc** triển khai cloud": nhóm đã **triển khai cloud thật**, nên điểm này không phụ thuộc AI.
- Mô hình Random Forest hiện huấn luyện trên **dữ liệu tổng hợp**; dữ liệu radar/GIS/mực sông là **mô phỏng**. Khi trình bày phải nói rõ, không nói "tích hợp radar thật".
- Nếu còn thời gian: đổ nước vào ống nhiều lần với tốc độ khác nhau, dữ liệu tự lưu trên cloud (`measurements`) → huấn luyện lại mô hình dự báo mực nước sau 1–3 phút trên **dữ liệu thật**, so sánh sai số với cách ngoại suy tuyến tính. Đây là cách trình bày AI thuyết phục nhất.

---

## 8. Báo cáo & slide

**Báo cáo cuối kỳ** cần thêm so với bản đề xuất:
1. Chuyển "dự kiến" → "đã thực hiện"; cập nhật topic (README mục 7), ngưỡng (README mục 8), kiến trúc có tầng cloud.
2. Mục **"Vì sao đây là hệ thống IoT"** (bảng mục 3).
3. Mục **Thực nghiệm** — đo thật và điền:

| Phép thử | Cách đo | Kết quả |
|---|---|---|
| Sai số cảm biến | so với thước ở 5 mức nước, mỗi mức 10 lần | ± … cm |
| Độ trễ đèn | từ lúc mức ổn định đến lúc đèn đổi (quay video, đếm khung hình) | … s (yêu cầu < 3 s sau thời gian ổn định) |
| Độ trễ thông báo đẩy | từ lúc đổi mức trên dashboard đến lúc điện thoại rung | … s (yêu cầu < 10 s) |
| Chống báo sai | đưa tay/vật qua cảm biến 10 lần | … / 10 lần không đổi mức |
| Mất mạng | tắt hotspot khi đang ngập | đèn vẫn đúng; thời gian báo offline … s |
| Thời gian báo trước | từ lúc cống ĐỎ đến lúc đường VÀNG (KPI trên console) | … phút |
| Chi phí | bảng linh kiện thực tế | … đ (< 1 triệu) |

4. Hạn chế trung thực: quy mô 2 node, bridge chưa TLS, AI trên dữ liệu tổng hợp, vị trí bản đồ minh hoạ.

**Slide (≤ 15):** 1 Bìa · 2 Bài toán · 3 Khoảng trống · 4 Giải pháp · 5 Vì sao là IoT · 6 Kiến trúc 4 tầng · 7 Phần cứng & mô hình · 8 Thuật toán (tốc độ dâng, hysteresis, ổn định 20 s) · 9 Edge: kết hợp 2 node & quy trình sự cố · 10 Cloud & web app · 11 Quản lý thiết bị từ xa · 12 Kết quả thực nghiệm · 13 So sánh với nghiên cứu khác · 14 Hạn chế & hướng phát triển · 15 Demo / cảm ơn.

---

## 9. Phân công còn lại

| Thành viên | Việc |
|---|---|
| Đức | Lắp mạch 1 ESP32 + 2 cảm biến (`MODE_DUAL`, còi **GPIO 25**, cảm biến 2 ở GPIO 19/21), nạp `flood_node`, mô hình fomex, đo sai số cảm biến |
| Huy | Chạy `start_all.bat`, thử đủ kịch bản mục 4, bridge lên cloud, đo độ trễ thông báo đẩy |
| Vân Anh | Tạo VM + `docker compose up`, kiểm tra web trên 4G, báo cáo + slide, dựng video |
