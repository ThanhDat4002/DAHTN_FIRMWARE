# ÔN TẬP EVION_Firmware (Bản Chi Tiết)

Cập nhật: 17/05/2026

## 1) Đọc tài liệu này như thế nào
Tài liệu này viết cho người mới vào dự án, mục tiêu là:
- Hiểu hệ thống chạy song song ra sao (task, core, priority).
- Hiểu dữ liệu đi qua mutex/queue/event group như thế nào.
- Hiểu luồng nghiệp vụ từ lúc boot đến lúc sạc, lỗi, OTA.
- Đủ tự tin trace bug theo log và theo call-flow.

Nếu mới hoàn toàn, đọc theo thứ tự:
1. Phần 2 (bức tranh tổng thể).
2. Phần 3 và 4 (task + tài nguyên đồng bộ).
3. Phần 5 (luồng dữ liệu chi tiết).
4. Phần 6 (giao thức).
5. Phần 8 (bản đồ file).

## 2) Bức tranh tổng thể hệ thống
Firmware chạy trên ESP32 (2 core), chia 2 cụm:
- Core hardware: đo đạc, safety, UI.
- Core network: Wi-Fi, cloud, polling command.

Mục tiêu vận hành:
- Luôn ưu tiên an toàn điện trước mọi tính năng cloud.
- Có mạng thì đồng bộ cloud.
- Mất mạng vẫn giữ logic safety local và chuyển OFFLINE.
- Có OTA thì cập nhật an toàn qua partition OTA.

## 3) Runtime model: task, core, priority
Task được tạo trong `app_main`:

| Task | Core | Priority | Vai trò |
|---|---:|---:|---|
| `safety_task` | 1 | 6 | Giám sát an toàn, quyết định dừng/FAULT |
| `sensor_task` | 1 | 5 | Đọc PZEM theo chu kỳ 200ms |
| `ui_task` | 1 | 2 | LED/buzzer theo state |
| `cloud_publish_task` | 0 | 4 | Gửi telemetry/status/heartbeat |
| `cloud_poll_task` | 0 | 4 | Đọc command/config từ Firebase |
| `wifi_reset_button_task` | 0 | 1 | Debounce + xử lý reset/master reset |
| `network_task` | 0 | 3 | Reconnect/backoff/provisioning fallback |
| `app_main` loop | không pin riêng | - | Uptime + WDT + rollback-valid |

Ý nghĩa thiết kế:
- `safety_task` có priority cao nhất để phản ứng nhanh với lỗi điện.
- `sensor_task` chạy nhanh để cấp dữ liệu mới cho safety.
- Cloud không chặn safety vì tách core và có mutex riêng cho HTTP.

## 4) Đồng bộ dữ liệu: mutex, queue, event group

## 4.1 Danh sách tài nguyên dùng chung

| Tài nguyên | Kiểu | Chủ sở hữu logic | Dùng để làm gì |
|---|---|---|---|
| `g_state_mutex` | Mutex | `globals.c` | Bảo vệ `g_system_state`, `g_error_code`, `g_network_status`, `g_max_current_limit` |
| `g_sensor_data_mutex` | Mutex | `globals.c` | Bảo vệ snapshot `g_latest_sensor_data` |
| `g_session_mutex` | Mutex | `globals_session.c` | Bảo vệ `g_current_session` và bộ tích phân năng lượng |
| `s_relay_mutex` | Mutex | `relay.c` | Bảo vệ trạng thái relay nội bộ `s_relay_state` |
| `s_firebase_mutex` | Mutex | `cloud_http.c` | Serialize toàn bộ HTTP request Firebase/backend |
| `g_sensor_queue` | Queue len=1 | producer `sensor_task`, consumer `safety_task` | Truyền sample sensor mới nhất |
| `s_beep_queue` | Queue len=4 | producer mọi module gọi `beep()`, consumer `ui_task` | Hàng đợi pattern còi |
| `g_network_event_group` | EventGroup | Wi-Fi/Cloud modules | Bit network: `WIFI_CONNECTED`, `FIREBASE_OK` |

## 4.2 Luồng `g_sensor_queue` (quan trọng nhất cho safety)
Thiết kế queue độ dài 1 và dùng `xQueueOverwrite`:
- `sensor_task` đọc được sample hợp lệ: ghi đè sample mới vào queue.
- `safety_task` luôn nhận sample mới nhất, không bị backlog sample cũ.
- Khi sensor lỗi: `sensor_publish_error_sample()` vẫn đẩy bản ghi `valid=false` để safety biết trạng thái lỗi.

Ý nghĩa:
- Hệ thống ưu tiên “real-time latest value” thay vì xử lý mọi sample lịch sử.
- Tránh việc `safety_task` bị trễ khi sensor chạy nhanh hơn safety.

## 4.3 Luồng `g_session_mutex`
Các hàm chạm `g_current_session` đều khóa mutex này:
- `globals_begin_session()` mở phiên.
- `globals_finish_session()` đóng phiên.
- `globals_update_session_energy()` tích phân năng lượng.
- `globals_get_session_snapshot()` trả snapshot an toàn cho task khác.

Điểm quan trọng cho người mới:
- Trong `globals_begin_session()` có comment cảnh báo deadlock: phải đọc sensor trước khi giữ `g_session_mutex`.
- Lý do: `globals_update_sensor_data()` có thể đi theo đường `sensor_data_mutex -> session_mutex`.
- Nếu code khác đi ngược `session_mutex -> sensor_data_mutex` có thể deadlock.

Quy tắc vàng hiện tại:
- Không tự ý giữ 2 mutex chéo thứ tự.
- Nếu cần cả sensor và session, lấy snapshot sensor trước, rồi mới xử lý session.

## 4.4 Luồng `s_firebase_mutex` trong `cloud_http.c`
`cloud_publish_task` và `cloud_poll_task` chạy song song, đều gọi HTTP.
Để tránh tranh chấp `esp_http_client`:
- `cloud_http_request()` sẽ chờ lock `s_firebase_mutex` theo timeout.
- Nếu lock bận quá lâu -> trả `ESP_ERR_TIMEOUT`.
- Các task cloud coi timeout kiểu này là “busy”, không phải lỗi mạng cứng.

Kết quả:
- Không có 2 request đập nhau.
- Tránh race condition trong layer HTTP.

## 4.5 EventGroup network bits
Bit trong `g_network_event_group`:
- `NETWORK_WIFI_CONNECTED_BIT`: set khi STA có IP, clear khi disconnect.
- `NETWORK_FIREBASE_OK_BIT`: set khi cloud reachable ổn định, clear khi fail vượt ngưỡng.

Ý nghĩa thực tế:
- Wi-Fi có thể connected nhưng cloud vẫn degraded.
- State machine phân biệt rõ `NET_WIFI_CONNECTED` và `NET_ONLINE`.

## 5) Luồng hoạt động chi tiết theo vòng lặp

## 5.1 Boot và khởi tạo
Trong `app_main`:
1. `nvs_manager_init()` phải chạy đầu tiên.
2. Cấu hình Task WDT.
3. `globals_init()` tạo mutex/queue/event group.
4. Nạp `maxCurrent` runtime từ NVS, clamp về dải cho phép.
5. Init phần cứng (`relay`, `ui`, `sensor`).
6. Recovery session cũ trong NVS (đang dở hoặc đã kết thúc chưa clear).
7. Init Wi-Fi và Cloud.
8. Tạo toàn bộ task, vào vòng lặp uptime.

## 5.2 Sensor -> Globals -> Safety (đường dữ liệu chính)
Chu kỳ `sensor_task`:
1. Quyết định có cần sample không (`sensor_should_sample`).
2. Gửi frame Modbus RTU qua UART.
3. Đọc response, kiểm tra độ dài + CRC.
4. Validate ngưỡng vật lý (V/A/W hợp lý).
5. Nếu hợp lệ:
   - `globals_update_sensor_data(&data)` cập nhật snapshot chung.
   - `xQueueOverwrite(g_sensor_queue, &data)` đẩy cho safety.
6. Nếu lỗi:
   - đẩy sample `valid=false` vào queue.
   - tăng error_count, đủ ngưỡng thì recover UART.

`safety_task`:
1. `xQueueReceive(g_sensor_queue, timeout=500ms)`.
2. Nếu sample invalid: xử lý grace, đếm lỗi, fault khi vượt ngưỡng.
3. Nếu sample valid: chạy chuỗi check theo thứ tự.

Thứ tự check safety hiện tại:
1. Relay stuck-on.
2. Over-current.
3. Over-voltage / under-voltage.
4. Đạt target năng lượng.
5. Unplug / no-load.
6. Trickle full-charge.

Nếu check nào “đã xử lý xong” sẽ `continue` vòng mới ngay.

## 5.3 Session và tích phân năng lượng
`globals_update_session_energy()` dùng quy tắc hình thang:
- Tính công suất trung bình giữa mẫu cũ và mới.
- Tích phân theo bước 1 giây (`ENERGY_INTEGRATION_STEP_MS`).
- Cộng vào `energy_used_kwh`.

Vì sao không cộng trực tiếp mỗi 200ms:
- Tránh drift do jitter timestamp.
- Dễ kiểm soát sai số theo bước cố định.

## 5.4 Wi-Fi reconnect và provisioning fallback
`network_task` chạy theo tick `WIFI_MANAGER_TICK_MS`:
1. Nếu đã có config mới từ portal -> restart.
2. Nếu chưa có STA config -> bật portal khi an toàn.
3. Nếu đang chờ provisioning fallback -> giữ/khởi động portal.
4. Nếu đã connected -> reset backoff.
5. Nếu mất mạng:
   - chuyển state `CHARGING -> OFFLINE` hoặc `READY -> IDLE`.
   - tăng retry và exponential backoff + jitter.
   - nếu quá ngưỡng retry hoặc disconnect reason xấu -> yêu cầu provisioning fallback.

Điểm an toàn:
- Không vào captive portal khi đang sạc hoặc session active.

## 5.5 Cloud publish/poll và degrade online
`cloud_publish_task`:
- Tạo payload gồm `config`, `telemetry`, `status`, `current_session`, `heartbeat`, `info`.
- PATCH lên Firebase theo chu kỳ.

`cloud_poll_task`:
- GET `/command` mỗi nhịp poll.
- GET `/config` theo divider (không phải nhịp nào cũng gọi).
- Parse/dispatch trong `cloud_command`.

`cloud_config_set_online()`:
- Có cơ chế failure streak trước khi hạ từ ONLINE xuống degraded.
- Tránh flap online/offline khi lỗi mạng thoáng qua.

## 5.6 Command handling
`cloud_command.c` làm 4 việc chính:
1. Parse payload JSON command.
2. Dedup command ID (chống xử lý lại lệnh cũ).
3. Map string -> enum command.
4. Execute action (relay start/stop, OTA, update config).

Cập nhật `maxCurrent`:
- Có clamp min/max.
- Áp dụng runtime ngay.
- Lưu NVS để reboot vẫn giữ.
- Nếu đang có session active thì cập nhật luôn giới hạn session.

## 5.7 OTA flow
`ota.c` + `ota_firebase.c`:
1. Lấy metadata URL/version từ Firebase.
2. Kiểm tra URL hợp lệ, kiểm tra header firmware.
3. So version/project name để tránh cập nhật sai.
4. Thực hiện `esp_https_ota_begin/perform/finish`.
5. Publish state OTA lên Firebase.

## 6) Giao thức và công nghệ đang dùng

| Nhóm | Cụ thể | Nơi dùng |
|---|---|---|
| Wi-Fi STA/AP | 802.11 | `wifi.c`, `wifi_portal.c` |
| HTTP server local | `esp_http_server` | Captive portal trong `wifi_portal.c` |
| DNS UDP | socket `recvfrom/sendto` port 53 | `dns_server.c` |
| HTTPS REST Firebase | `esp_http_client` | `cloud_http.c`, `cloud.c` |
| HTTPS report backend | POST + header chữ ký | `cloud_report.c` |
| HMAC-SHA256 | `mbedtls_md_hmac` | `cloud_report.c` |
| SNTP/NTP | `esp_netif_sntp_*` | `cloud_config.c` |
| UART Modbus RTU | PZEM-004T | `sensor.c` |
| OTA HTTPS | `esp_https_ota` | `ota.c` |
| NVS | key-value flash | `nvs_manager.c` |
| RTOS primitives | Queue/Mutex/EventGroup | `globals.c`, `cloud_http.c`, `relay.c`, `ui.c` |

## 7) State machine thực tế (điều người mới hay nhầm)
Các trạng thái chính:
- `BOOT`: đang khởi tạo.
- `IDLE`: chưa sẵn sàng sạc (ví dụ chưa cloud online).
- `READY`: sẵn sàng nhận lệnh sạc.
- `CHARGING`: đang sạc bình thường.
- `OFFLINE`: vẫn đang sạc nhưng cloud mất.
- `STOPPED`: vừa dừng phiên hợp lệ.
- `FAULT`: dừng do lỗi an toàn.

Ví dụ chuyển trạng thái:
- Wi-Fi + Firebase OK và không sạc -> `READY`.
- Đang `READY` mà Wi-Fi mất -> `IDLE`.
- Đang `CHARGING` mà cloud mất -> `OFFLINE`.
- Safety phát hiện quá dòng -> `FAULT`.

## 8) Bản đồ file đầy đủ (vai trò + dữ liệu vào/ra)

## 8.1 Root
| File | Vai trò | Input/Output chính |
|---|---|---|
| `AGENTS.md` | Tài liệu kiến trúc/quy ước | Input cho dev/agent |
| `CMakeLists.txt` | Build top-level ESP-IDF | Output target build |
| `sdkconfig` | Cấu hình tính năng IDF | Ảnh hưởng toàn firmware |
| `partitions_8mb.csv` | Phân vùng flash OTA/NVS | Ảnh hưởng OTA/flash layout |
| `kiemtra.md` | Ghi chú audit | Input tham khảo |
| `MÔ TẢ PHẦN MỀM HỆ THỐNG ESP32 mới .md` | Tài liệu nghiệp vụ | Input tham khảo |
| `.clangd`, `.gitignore` | Tooling | Không ảnh hưởng runtime |

## 8.2 `cmake/`
| File | Vai trò |
|---|---|
| `cmake/windows-c-rules-override.cmake` | Override luật build C trên Windows |

## 8.3 `tools/`
| File | Vai trò |
|---|---|
| `tools/idf-build-patched.ps1` | Build script chính cho môi trường local |
| `tools/patch-ar-rules.ps1` | Vá rule `ar` khi build |
| `tools/evion-ar-wrapper.ps1` | Wrapper toolchain `ar` (PowerShell) |
| `tools/evion-ar-wrapper.cmd` | Wrapper toolchain `ar` (CMD) |

## 8.4 `main/` runtime (mô tả chi tiết từng file)

### `main/CMakeLists.txt`
- Đăng ký toàn bộ source `.c` của component `main` để build thành `libmain.a`.
- Khai báo các dependency bắt buộc: `driver`, `esp_wifi`, `esp_http_client`, `esp_https_ota`, `nvs_flash`, `json`, `mbedtls`.
- Quyết định module nào thực sự được link vào firmware, nên ảnh hưởng trực tiếp kích thước binary.
- Là nơi đầu tiên phải cập nhật khi thêm file `.c` mới, nếu quên thì code không được biên dịch.
- Giúp nhìn nhanh kiến trúc module thật sự đang active trong runtime hiện tại.

### `main/config.h`
- Chứa hằng số cấu hình trung tâm: GPIO pin, chu kỳ task, ngưỡng an toàn, timeout mạng, key NVS.
- Các task runtime đọc trực tiếp giá trị tại đây nên đổi config có thể thay đổi hành vi toàn hệ thống.
- Là nơi định nghĩa giới hạn kỹ thuật như `MAX_CURRENT_A`, `MIN_VOLTAGE_V`, `MAX_VOLTAGE_V`, `TRICKLE_CURRENT_A`.
- Chứa tham số Wi-Fi reconnect/backoff và chu kỳ cloud publish/poll/heartbeat.
- Khi debug sai hành vi, luôn kiểm tra `config.h` trước vì nhiều lỗi do ngưỡng chưa phù hợp thực tế.

### `main/types.h`
- Khai báo enum trạng thái hệ thống (`BOOT/IDLE/READY/CHARGING/OFFLINE/STOPPED/FAULT`).
- Định nghĩa struct dữ liệu sensor (`pzem_data_t`) dùng xuyên suốt sensor, safety, cloud.
- Định nghĩa struct session sạc (`charging_session_t`) làm chuẩn dữ liệu lưu NVS và report cloud.
- Chứa kiểu command/config giúp parser cloud và các module xử lý cùng một schema.
- Là hợp đồng dữ liệu (data contract) giữa các module, sửa kiểu ở đây phải rà toàn bộ call-site.

### `main/main.c`
- Là entry point `app_main`, điều phối thứ tự init bắt buộc: NVS -> globals -> phần cứng -> Wi-Fi/Cloud -> task.
- Tạo và pin task lên từng core với priority khác nhau để tách workload hardware và network.
- Khôi phục `maxCurrent` từ NVS và clamp lại để tránh giá trị lỗi sau reboot.
- Xử lý recovery session cũ sau mất điện, dọn session dở để tránh trạng thái không nhất quán.
- Quản lý watchdog task chính và logic đánh dấu app hợp lệ cho rollback OTA.

### `main/globals.h/.c`
- Định nghĩa toàn bộ biến trạng thái dùng chung (`g_system_state`, `g_error_code`, `g_network_status`, `g_max_current_limit`).
- Tạo và quản lý các primitive đồng bộ: `g_state_mutex`, `g_sensor_data_mutex`, `g_session_mutex`, `g_sensor_queue`, `g_network_event_group`.
- Cung cấp API getter/setter thread-safe để hạn chế ghi trực tiếp biến global từ nhiều task.
- Duy trì snapshot sensor mới nhất và cung cấp fallback snapshot khi mutex timeout.
- Là “bus trạng thái trung tâm” của firmware, hầu hết module đều phụ thuộc vào module này.

### `main/globals_session.h/.c`
- Quản lý vòng đời phiên sạc: mở phiên, cập nhật năng lượng, đóng phiên, lấy snapshot phiên.
- Bảo vệ `g_current_session` bằng `g_session_mutex` để tránh race condition giữa safety/cloud/command.
- Tích phân năng lượng theo timestamp sample bằng phương pháp hình thang, giảm sai số tích lũy.
- Lưu session ra NVS ở các điểm quan trọng để có thể recovery sau reboot/mất điện.
- Có lưu ý thứ tự lock để tránh deadlock khi đồng thời đụng session và sensor data.

### `main/nvs_manager.h/.c`
- Là lớp truy cập flash NVS tập trung cho Wi-Fi config, cloud config, runtime current limit và session.
- Đóng gói logic serialize/deserialize dữ liệu session, bao gồm đổi đơn vị năng lượng/dòng về dạng lưu trữ.
- Cung cấp API `save/load/clear` rõ ràng giúp module khác không cần thao tác NVS trực tiếp.
- Xử lý tình huống NVS cần erase/re-init khi metadata flash lỗi.
- Là điểm then chốt cho tính bền vững dữ liệu sau reboot và giữa các lần cập nhật firmware.

### `main/wifi.h/.c`
- Quản lý kết nối STA: start, reconnect, exponential backoff + jitter và chuyển trạng thái mạng toàn cục.
- Xử lý event Wi-Fi/IP từ event loop và cập nhật event bits tương ứng.
- Chạy `network_task` để điều phối retry, fallback provisioning và chuyển `READY/IDLE/OFFLINE` theo ngữ cảnh.
- Chạy `wifi_reset_button_task` để xử lý giữ nút reset Wi-Fi và combo master reset.
- Chặn vào captive portal khi đang sạc/session active để không ảnh hưởng an toàn vận hành.

### `main/wifi_internal.h`
- Định nghĩa interface nội bộ giữa `wifi.c` và `wifi_portal.c` để chia sẻ helper mà không public ra module khác.
- Giúp tách logic portal ra file riêng nhưng vẫn thao tác được state nội bộ Wi-Fi đúng quy tắc.
- Tránh include vòng và tránh lộ implementation detail qua `wifi.h`.
- Là ranh giới kỹ thuật giúp bảo trì code dễ hơn khi đổi logic provisioning.
- Khi sửa provisioning flow, cần kiểm tra tương thích cả 2 phía dùng header này.

### `main/wifi_portal.c`
- Dựng AP cấu hình (`EVION_Setup`) khi thiếu cấu hình hoặc cần provisioning fallback.
- Chạy HTTP server local để nhận form SSID/password/firebase config từ người dùng.
- Lưu cấu hình mới xuống NVS rồi báo `wifi.c` chuẩn bị restart thiết bị.
- Tích hợp DNS redirect để điện thoại mở trang portal tự động mà không cần nhập IP thủ công.
- Có cơ chế idempotent để tránh start portal lặp khi task mạng kiểm tra nhiều vòng.

### `main/dns_server.h/.c`
- Triển khai DNS server UDP port 53 ở chế độ AP captive portal.
- Parse query DNS và phản hồi cố định về IP AP của thiết bị.
- Giúp mọi domain truy vấn trên điện thoại đều trỏ về portal cấu hình.
- Chạy theo task riêng, xử lý recv/send socket không chặn luồng điều khiển chính.
- Là thành phần phụ trợ quan trọng để trải nghiệm provisioning mượt với người dùng mới.

### `main/cloud.h`
- Khai báo API public của module cloud để `main.c` và module khác gọi thống nhất.
- Tách rõ boundary public/private, tránh phụ thuộc trực tiếp vào struct/hàm nội bộ cloud.
- Đảm bảo thay đổi implementation cloud không làm vỡ module bên ngoài nếu API giữ ổn định.
- Là điểm tham chiếu đầu tiên khi muốn tích hợp thêm tính năng cloud ở module khác.
- Giữ interface tối giản giúp giảm coupling giữa business logic và transport layer.

### `main/cloud_internal.h`
- Khai báo interface nội bộ giữa `cloud.c`, `cloud_http.c`, `cloud_config.c`, `cloud_command.c`, `cloud_report.c`.
- Chứa struct/cache/hằng số chỉ dùng trong subsystem cloud, không expose ra ngoài.
- Định nghĩa contract transport HTTP, parser command và report signing trong phạm vi nội bộ.
- Giúp các file cloud trao đổi dữ liệu mà vẫn tách module theo trách nhiệm riêng.
- Là nơi quan trọng để hiểu “xương sống” cloud trước khi đọc từng file implementation.

### `main/cloud.c`
- Khởi tạo cloud subsystem, nạp config cloud và kiểm tra khả năng truy cập Firebase lúc boot.
- Chạy `cloud_publish_task` để gửi telemetry/status/session/heartbeat theo chu kỳ.
- Chạy `cloud_poll_task` để đọc command/config, gọi parser và apply config runtime.
- Cập nhật trạng thái online/degraded dựa trên kết quả request và tín hiệu busy/timeout.
- Là coordinator cloud-level, còn transport/config/parser/report được tách sang file chuyên biệt.

### `main/cloud_http.c`
- Cung cấp lớp HTTP chung cho cloud với mutex serialize (`s_firebase_mutex`) để chống tranh chấp request.
- Đóng gói `esp_http_client` và event handler đọc response buffer an toàn theo kích thước giới hạn.
- Hỗ trợ method GET/PATCH/PUT/POST và header tùy chọn cho các API khác nhau.
- Có retry policy và timeout rõ ràng, phân biệt lỗi thật với tình huống mutex bận.
- Là điểm duy nhất nên chạm khi cần đổi chiến lược retry/timeout/HTTP header toàn cloud.

### `main/cloud_config.c`
- Nạp config cloud theo thứ tự ưu tiên: hardcoded defaults -> override từ NVS.
- Build URL Firebase theo `device_id` và `auth token` để các request cloud dùng nhất quán.
- Đồng bộ thời gian thực qua SNTP để phục vụ chữ ký HMAC/report timestamp.
- Quản lý trạng thái online/degraded có debounce bằng failure streak để tránh flap trạng thái.
- Chịu trách nhiệm chuyển đổi network state khi cloud lỗi trong lúc đang sạc hoặc đang idle.

### `main/cloud_command.c`
- Parse JSON command/config từ Firebase và map string command thành enum xử lý.
- Chống xử lý lệnh trùng bằng dedup command ID.
- Áp dụng thay đổi runtime như `maxCurrent`, clamp về dải an toàn và lưu NVS.
- Dispatch lệnh điều khiển relay/sạc/OTA theo state hiện tại và điều kiện an toàn.
- Cập nhật acknowledgement/status command để cloud biết lệnh đã được xử lý hay bị từ chối.

### `main/cloud_report.c`
- Gửi báo cáo kết thúc phiên sạc lên backend HTTP riêng (không phải node telemetry Firebase thông thường).
- Ký request bằng HMAC-SHA256 khi `STATION_REPORT_SECRET` có cấu hình.
- Bổ sung timestamp/nonce/signature header để backend chống replay và xác thực nguồn gửi.
- Retry theo chu kỳ từ luồng cloud publish khi chưa gửi thành công.
- Khi report thành công cho session đã kết thúc thì xóa session khỏi NVS để tránh gửi lặp.

### `main/relay.h/.c`
- Quản lý bật/tắt relay qua GPIO, đặt trạng thái OFF an toàn khi boot.
- Dùng `s_relay_mutex` để bảo vệ đọc/ghi trạng thái relay trong môi trường đa task.
- Cung cấp API gọn `relay_on`, `relay_off`, `relay_is_on` cho safety/cloud/wifi dùng chung.
- Khi bật relay có gọi `sensor_wakeup_now()` để sensor lấy mẫu sớm sau thay đổi tải.
- Là điểm chạm phần cứng công suất trực tiếp nên mọi thay đổi phải đi kèm test an toàn.

### `main/sensor.h/.c`
- Giao tiếp PZEM-004T qua UART Modbus RTU: gửi request, nhận response, kiểm CRC frame.
- Validate dữ liệu vật lý để loại mẫu outlier trước khi đưa vào logic safety/cloud.
- Đẩy sample mới nhất vào `g_sensor_queue` bằng `xQueueOverwrite` để safety luôn thấy dữ liệu mới nhất.
- Cập nhật snapshot sensor toàn cục và hỗ trợ publish sample lỗi `valid=false`.
- Có cơ chế tự recover UART sau nhiều lỗi liên tiếp để giảm downtime cảm biến.

### `main/safety.h/.c`
- Là lõi an toàn hệ thống, tiêu thụ dữ liệu từ queue sensor và quyết định STOP/FAULT.
- Kiểm tra nhiều lớp: invalid sensor, relay stuck, quá dòng, quá áp/thấp áp, unplug/no-load, trickle full.
- Quản lý state nội bộ theo streak/counter/grace window để chống false positive do nhiễu tức thời.
- Gọi `globals_finish_session()` và `relay_off()` khi cần dừng, đồng thời phát beep cảnh báo qua UI.
- Định kỳ lưu session NVS khi đang sạc để giảm mất dữ liệu nếu mất nguồn đột ngột.

### `main/ui.h/.c`
- Điều khiển LED trạng thái và buzzer theo state machine hệ thống.
- Dùng queue beep (`s_beep_queue`) để không chặn luồng gọi beep từ module khác.
- `ui_task` nhận beep pattern và phát theo thứ tự, có cơ chế drop-oldest khi queue đầy.
- Mapping state -> pattern LED giúp người vận hành nhìn nhanh tình trạng thiết bị.
- Chạy priority thấp hơn safety/sensor để không ảnh hưởng phản ứng an toàn thời gian thực.

### `main/ota.h/.c`
- Triển khai luồng OTA chính bằng `esp_https_ota` (begin, read image desc, perform, finish/abort).
- Kiểm tra project name/version trước khi ghi flash để tránh update nhầm firmware.
- Đọc firmware URL và target version từ cloud metadata, có fallback path khi key chính thiếu.
- Publish trạng thái lỗi/thành công OTA để backend quan sát tiến trình từ xa.
- Tổ chức OTA dạng task riêng giúp không khóa cứng các luồng runtime khác.

### `main/ota_firebase.h/.c`
- Cung cấp helper chuyên biệt cho OTA khi đọc/ghi metadata trên Firebase.
- Build station path OTA nhất quán để `ota.c` không phải lặp logic path format.
- Gửi progress/state OTA có throttle để tránh spam request quá dày.
- Dùng `esp_http_client` cho GET/PATCH OTA metadata độc lập với parser command thông thường.
- Là lớp cầu nối giữa cloud data model và engine OTA thực thi trong `ota.c`.

## 9) Các thông số runtime quan trọng (hiện tại)
- Sensor tick: `200ms`.
- Cloud publish: `5000ms`.
- Cloud poll: `1000ms`.
- Heartbeat: `5000ms`.
- Wi-Fi retry max: `5`, backoff `2000ms -> 60000ms` có jitter.
- Over-current mặc định: `1.5A` (có thể override từ cloud/NVS).
- Voltage guard: `180V..380V`.
- Trickle cutoff: `0.005A` trong `5 phút`.

## 10) Hướng dẫn debug theo triệu chứng

## 10.1 Mất mạng liên tục
- Xem `wifi_event_handler` log reason disconnect.
- Kiểm tra `network_task` có vào provisioning fallback không.
- Kiểm tra bit `NETWORK_WIFI_CONNECTED_BIT` và `NETWORK_FIREBASE_OK_BIT`.

## 10.2 Có dữ liệu sensor nhưng safety không phản ứng
- Kiểm tra `sensor_task` có `xQueueOverwrite` đều không.
- Kiểm tra `safety_task` có timeout `xQueueReceive` thường xuyên không.
- Kiểm tra sample `valid=false` do CRC/length/outlier.

## 10.3 Lệnh cloud đến nhưng không chạy
- Kiểm tra `cloud_poll_task` đọc được `/command` chưa.
- Kiểm tra dedup command ID có bỏ qua command trùng không.
- Kiểm tra trạng thái chặn an toàn (đang FAULT, đang session, chặn OTA...).

## 10.4 Session sai năng lượng
- Kiểm tra timestamp sensor và nhịp sample.
- Kiểm tra logic tích phân trong `globals_update_session_energy()`.
- Kiểm tra NVS save/load session có bị stale data không.

## 11) Checklist khi sửa code để không phá hệ thống
1. Không đổi thứ tự lock mutex tùy tiện.
2. Không để task nào chạy vòng kín không `vTaskDelay` hoặc không reset WDT.
3. Không bỏ `xQueueOverwrite` của sensor nếu chưa thiết kế lại safety.
4. Sửa ngưỡng safety phải test trên thiết bị thật.
5. Sửa cloud retry/timeout phải test cả mạng chập chờn.
6. Không sửa artifact trong `build/` và `build_fresh/`.

## 12) Lệnh thực hành nhanh
1. Build: `idf.py build` hoặc task VSCode patched build.
2. Flash: `idf.py -p COMx flash`.
3. Monitor: `idf.py monitor`.
4. Clean: `idf.py fullclean`.
