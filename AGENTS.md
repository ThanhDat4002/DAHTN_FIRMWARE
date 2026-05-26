# NGỮ CẢNH DỰ ÁN (AGENTS)

## 1. Tổng quan dự án
- **Tên dự án**: EVION_Firmware.
- **Loại dự án**: firmware nhúng cho vi điều khiển ESP32, viết bằng C/C++ theo ESP-IDF (Espressif IoT Development Framework).
- **Mục tiêu chung**: xây dựng hệ thống quản lý thông minh bao gồm:
  - Kết nối và quản lý Wi-Fi với giao diện cấu hình captive portal.
  - Giao tiếp hai chiều với cloud backend (đồng bộ trạng thái, nhận lệnh, báo cáo).
  - Cung cấp cơ chế cập nhật firmware OTA (Over-The-Air) an toàn.
  - Điều khiển relay và đọc dữ liệu từ cảm biến.
  - Phản hồi người dùng qua giao diện UI.
  - Đảm bảo tính an toàn hệ thống với các cơ chế bảo vệ.
- **Phạm vi**: tập trung vào logic firmware, không sửa tay các file trong thư mục build.
- **Tập tin cấu hình quan trọng**: [sdkconfig](sdkconfig) (cấu hình ESP-IDF), [partitions_8mb.csv](partitions_8mb.csv) (bố cục phân vùng flash).

## 2. Kiến trúc chức năng

### 2.1 Khối kết nối và mạng
- **Wi-Fi Management** (các file [main/wifi.c](main/wifi.c), [main/wifi.h](main/wifi.h), [main/wifi_internal.h](main/wifi_internal.h)):
  - Quản lý trạng thái kết nối Wi-Fi (kết nối, tìm kiếm, mất kết nối).
  - Xử lý sự kiện từ ESP-IDF Wi-Fi event loop.
  - Lưu thông tin SSID/Password trong NVS.
  - Cơ chế reconnect tự động với exponential backoff.

- **Captive Portal** (file [main/wifi_portal.c](main/wifi_portal.c)):
  - Tạo mạng AP (access point) cho phép người dùng cấu hình Wi-Fi.
  - Phục vụ trang HTML cấu hình.
  - Xử lý HTTP POST để nhận thông tin Wi-Fi từ người dùng.

- **DNS Server** (các file [main/dns_server.c](main/dns_server.c), [main/dns_server.h](main/dns_server.h)):
  - Hỗ trợ captive portal bằng cách chuyển hướng tất cả truy vấn DNS tới IP của thiết bị.
  - Cho phép người dùng truy cập portal mà không cần biết IP chính xác.

### 2.2 Khối Cloud
- **Cloud Core** (file [main/cloud.c](main/cloud.c), [main/cloud.h](main/cloud.h)):
  - Quản lý vòng đời kết nối cloud (khởi tạo, kết nối, ngắt).
  - Điều phối các task/thread liên quan đến cloud.
  - Xử lý sự kiện kết nối/mất kết nối.

- **Cloud Commands** (file [main/cloud_command.c](main/cloud_command.c)):
  - Nhận các lệnh từ cloud server.
  - Phân tích và thực thi lệnh (ví dụ: bật/tắt relay, thay đổi cấu hình).
  - Gửi phản hồi về server.

- **Cloud Configuration** (file [main/cloud_config.c](main/cloud_config.c)):
  - Cấu hình máy chủ cloud, port, authentication.
  - Lưu trữ và quản lý chứng chỉ SSL/TLS.
  - Xử lý session và token.

- **Cloud HTTP** (file [main/cloud_http.c](main/cloud_http.c)):
  - Giao tiếp HTTP/HTTPS với cloud backend.
  - Quản lý request/response, timeouts.
  - Xử lý các lỗi mạng và reconnect.

- **Cloud Reporting** (file [main/cloud_report.c](main/cloud_report.c)):
  - Thu thập dữ liệu từ sensor, relay, trạng thái hệ thống.
  - Định kỳ gửi báo cáo về cloud (heartbeat, telemetry).
  - Đồng bộ trạng thái cục bộ với trạng thái cloud.

- **Cloud Internal** (file [main/cloud_internal.h](main/cloud_internal.h)):
  - Định nghĩa cấu trúc dữ liệu nội bộ, state machine, callbacks.

### 2.3 Khối OTA (Over-The-Air Update)
- **OTA Core** (các file [main/ota.c](main/ota.c), [main/ota.h](main/ota.h)):
  - Quản lý quy trình cập nhật firmware OTA.
  - Tải xuống firmware từ server.
  - Xác thực và ghi firmware vào flash partition.
  - Rollback an toàn nếu cập nhật thất bại.
  - Quản lý OTA task và event.

- **OTA Firebase** (các file [main/ota_firebase.c](main/ota_firebase.c), [main/ota_firebase.h](main/ota_firebase.h)):
  - Tích hợp với Firebase để kiểm tra phiên bản firmware mới.
  - Tải xuống file firmware từ Firebase Storage.
  - Xử lý các lỗi liên quan đến Firebase.

### 2.4 Khối điều khiển thiết bị
- **Relay Control** (các file [main/relay.c](main/relay.c), [main/relay.h](main/relay.h)):
  - Điều khiển các relay qua GPIO.
  - Quản lý trạng thái relay (bật/tắt).
  - Đồng bộ trạng thái relay với cloud.
  - Cơ chế scheduling/timer cho relay.

- **Sensor Readout** (các file [main/sensor.c](main/sensor.c), [main/sensor.h](main/sensor.h)):
  - Đọc giá trị từ các cảm biến (nhiệt độ, độ ẩm, v.v.).
  - Lọc, hiệu chuẩn dữ liệu cảm biến.
  - Quản lý ADC, UART, I2C để giao tiếp cảm biến.
  - Định kỳ cập nhật giá trị cảm biến.

### 2.5 Khối UI và phản hồi người dùng
- **UI Feedback** (các file [main/ui.c](main/ui.c), [main/ui.h](main/ui.h)):
  - Điều khiển LED, buzzer hoặc các chỉ báo trạng thái.
  - Phản hồi người dùng cho các sự kiện (Wi-Fi connected, cloud synced, etc.).
  - Hiển thị thông tin trạng thái qua UI.

### 2.6 Khối an toàn hệ thống
- **Safety Logic** (các file [main/safety.c](main/safety.c), [main/safety.h](main/safety.h)):
  - Kiểm tra các điều kiện bảo vệ (ví dụ: nhiệt độ quá cao, lỗi cảm biến).
  - Thực thi các hành động bảo vệ (ví dụ: tắt relay, khẩn cấp).
  - Ghi nhật ký sự cố.
  - Quản lý watchdog timer.

### 2.7 Khối lưu trữ và quản lý trạng thái
- **NVS Manager** (các file [main/nvs_manager.c](main/nvs_manager.c), [main/nvs_manager.h](main/nvs_manager.h)):
  - Lưu cấu hình (Wi-Fi credentials, cloud settings, etc.) vào NVS flash.
  - Cấu trúc key-value pairs cho dữ liệu persistent.
  - Xử lý lỗi flash, wear leveling.
  - API đơn giản cho đọc/ghi cấu hình.

- **Global State** (các file [main/globals.c](main/globals.c), [main/globals.h](main/globals.h)):
  - Tập trung các biến toàn cục của hệ thống.
  - Quản lý mutex/semaphore để thread-safe access.
  - Định nghĩa struct cho device state, config state.

- **Session State** (các file [main/globals_session.c](main/globals_session.c), [main/globals_session.h](main/globals_session.h)):
  - Lưu trạng thái phiên làm việc (kết nối cloud hiện tại, OTA progress, etc.).
  - Reset khi khởi động lại thiết bị.
  - Quản lý temporary data.

### 2.8 Điểm vào chính
- **Main Entry** (file [main/main.c](main/main.c)):
  - Khởi tạo tất cả các module theo thứ tự: NVS → Global State → Wi-Fi → Cloud → OTA → Sensor → Relay → UI.
  - Tạo các task/thread chính (Wi-Fi task, Cloud task, OTA task, Sensor task, UI task).
  - Vòng lặp chính (main loop) để giám sát và xử lý sự kiện toàn hệ thống.
## 3. Cấu trúc thư mục
```
EVION_Firmware/
├── [main/](main/)
│   ├── *.c, *.h              # Mã nguồn firmware: wifi, cloud, ota, relay, sensor, ui, safety, nvs, dns, globals
│   └── CMakeLists.txt        # Cấu hình CMake cho phần main
├── [cmake/](cmake/)
│   └── *.cmake               # Override CMake cho Windows (quy tắc build, compiler flags)
├── [tools/](tools/)
│   ├── idf-build-patched.ps1 # Script build chính (wrapper ESP-IDF)
│   ├── patch-ar-rules.ps1    # Script vá quy tắc ar cho Windows
│   └── *.cmd, *.ps1          # Script hỗ trợ khác
├── [build/](build/)
│   └── (build artifacts)     # Output từ lần build cuối, không chỉnh sửa
├── [build_fresh/](build_fresh/)
│   └── (clean build output)  # Output từ fresh build, không chỉnh sửa
├── CMakeLists.txt            # CMakeLists chính cấp dự án
├── [sdkconfig](sdkconfig)     # Cấu hình ESP-IDF (kích thước flash, features, etc.)
├── [partitions_8mb.csv](partitions_8mb.csv) # Bố cục phân vùng flash (bootloader, app, nvs, ota, spiffs)
└── README.md                 # Hướng dẫn sử dụng dự án
```

## 4. Build và phát hành

### 4.1 Quy trình Build
- **Cách build chuẩn trong VS Code**:
  1. Mở Command Palette (`Ctrl+Shift+P`).
  2. Chạy task: **ESP-IDF Build (Patched)**.
  3. Script sẽ tự động thiết lập environment, patch quy tắc build, và gọi ESP-IDF CMake.

- **Script tương ứng**: [tools/idf-build-patched.ps1](tools/idf-build-patched.ps1)
  - Thiết lập đường dẫn `IDF_PATH`, `IDF_PYTHON_ENV_PATH`, `PATH` để tìm cmake, ninja, xtensa-esp-elf.
  - Gọi script `patch-ar-rules.ps1` để vá lỗi quy tắc ar trên Windows.
  - Chạy `idf.py build` hoặc CMake build.

- **Thêm file C/C++ mới**:
  - Tạo file trong [main/](main/).
  - Cập nhật [main/CMakeLists.txt](main/CMakeLists.txt) để thêm file vào danh sách `SOURCES`:
    ```cmake
    idf_component_register(
        SRCS "wifi.c" "cloud.c" "ota.c" ... "new_file.c"
        INCLUDE_DIRS "."
    )
    ```

### 4.2 Clean Build
- Xóa thư mục [build/](build/) hoặc [build_fresh/](build_fresh/) rồi build lại để loại bỏ cache.
- Hoặc chạy `idf.py fullclean` trong PowerShell.

### 4.3 Flash thiết bị
- Sử dụng ESP-IDF công cụ: `idf.py flash` hoặc `esptool.py`.
- Cần kết nối thiết bị qua USB/Serial.
- Port mặc định: `COM3` (Windows) - có thể thay đổi.

### 4.4 Monitor output
- Sau flash, chạy: `idf.py monitor` để xem log từ device.
- Baudrate mặc định: 115200 bps.

### 4.5 OTA Update (Over-The-Air)
- Firmware được xây dựng với partition OTA (xem [partitions_8mb.csv](partitions_8mb.csv)).
- Cloud server gửi URL file firmware → thiết bị tải và xác thực → ghi vào partition OTA → reboot.
- Logic: xem [main/ota.c](main/ota.c), [main/ota_firebase.c](main/ota_firebase.c).
- Rollback: nếu app crashed sau update, boot sẽ revert về firmware cũ.

## 5. Quy ước thay đổi và cảnh báo

### 5.1 Tiêu chí bất biến
- **Không sửa** các tập tin trong [build/](build/) và [build_fresh/](build_fresh/) — chúng được tạo tự động.
- **Không sửa** tệp cấu hình tạo tự động như `config.env`, `CMakeCache.txt`.

### 5.2 Khi thay đổi logic Wi-Fi/Cloud/OTA
- Kiểm tra luồng khởi tạo trong [main/main.c](main/main.c) để đảm bảo thứ tự modules không bị phá vỡ.
- Đảm bảo các dependencies giữa các module vẫn được duy trì (ví dụ: Wi-Fi phải khởi tạo trước Cloud).
- Test toàn bộ chuỗi khởi động trên device sau khi thay đổi.

### 5.3 Khi thay đổi lưu trữ cấu hình
- Cập nhật [main/nvs_manager.c](main/nvs_manager.c) nếu thêm key-value mới.
- Cập nhật [main/nvs_manager.h](main/nvs_manager.h) nếu thêm hàm API.
- Kiểm tra xung đột với các key hiện tại để tránh overwrite dữ liệu cũ.

### 5.4 Khi thêm file C/C++ mới
- Tuân theo cấu trúc: 1 header (.h), 1 implementation (.c).
- Thêm vào [main/CMakeLists.txt](main/CMakeLists.txt).
- Nếu cần, tạo file `*_internal.h` cho các định nghĩa nội bộ (không export).

## 6. Quy ước code và style

### 6.1 Naming Convention
- **File**: snake_case, ví dụ `wifi.c`, `nvs_manager.c`, `cloud_report.h`.
- **Function**: snake_case, ví dụ `wifi_connect()`, `cloud_send_heartbeat()`.
- **Variable**: snake_case, ví dụ `connection_status`, `sensor_value`.
- **Struct/Type**: snake_case with `_t` suffix, ví dụ `device_config_t`, `cloud_state_t`.
- **Macro**: UPPER_SNAKE_CASE, ví dụ `WIFI_MAX_SSID_LEN`, `CLOUD_RETRY_TIMEOUT_MS`.

### 6.2 Logging
- Sử dụng ESP-IDF logging API: `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`, `ESP_LOGD()`.
- Log tag: module name + operation, ví dụ:
  ```c
  #define TAG "WiFi_Connect"
  #define TAG "Cloud_HTTP"
  #define TAG "OTA_Firebase"
  ```
- Ví dụ log:
  ```c
  ESP_LOGI(TAG, "Connected to SSID: %s", ssid);
  ESP_LOGW(TAG, "Retry connection, attempt %d", retry_count);
  ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
  ```

### 6.3 Error Handling
- Sử dụng `esp_err_t` từ ESP-IDF.
- Kiểm tra return code của mọi function:
  ```c
  esp_err_t ret = wifi_connect();
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Connection failed: %s", esp_err_to_name(ret));
      return ret;
  }
  ```

### 6.4 Thread-Safety
- Sử dụng mutex (từ FreeRTOS) để bảo vệ shared state:
  ```c
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  // Access shared state
  xSemaphoreGive(state_mutex);
  ```
- Xem ví dụ trong [main/globals.c](main/globals.c).

### 6.5 Memory Management
- Sử dụng `malloc()`, `free()` từ ESP-IDF hoặc các allocator được cấp hình.
- Luôn check return value của `malloc()`:
  ```c
  char *buffer = malloc(size);
  if (!buffer) {
      ESP_LOGE(TAG, "Failed to allocate memory");
      return;
  }
  ```

### 6.6 Code Style
- **Indentation**: 4 spaces (không tab).
- **Line length**: tối đa ~100 ký tự.
- **Braces**: K&R style:
  ```c
  if (condition) {
      // code
  } else {
      // code
  }
  ```
- **Comment**: comment trước dòng code hoặc ở cuối dòng nếu ngắn:
  ```c
  // Initialize Wi-Fi subsystem
  wifi_init();
  
  int status = 0;  // Connection status: 0=disconnected, 1=connected
  ```

## 7. Workflow và State Machine

### 7.1 Khởi động hệ thống (Bootstrap Flow)
Thứ tự khởi tạo trong [main/main.c](main/main.c):
1. **NVS Init** → Tải cấu hình từ flash (Wi-Fi credentials, cloud config, etc.)
2. **Global State Init** → Khởi tạo mutex, semaphore, shared variables
3. **Wi-Fi Init** → Thiết lập Wi-Fi module, kết nối SSID đã lưu (hoặc tạo AP nếu chưa có)
4. **Cloud Init** → Khởi tạo cloud module, tuy vào kết nối Wi-Fi
5. **OTA Init** → Chuẩn bị partition, kiểm tra phiên bản
6. **Sensor Init** → Khởi tạo ADC, UART, I2C
7. **Relay Init** → Khởi tạo GPIO, khôi phục trạng thái cuối cùng
8. **UI Init** → Khởi tạo LED/buzzer, hiển thị trạng thái startup
9. **Main Loop** → Giám sát event, xử lý task, watchdog

### 7.2 Wi-Fi Connection Flow
```
Startup → Try SSID saved
          ├─ Success → Connected (green LED)
          │           └─ Notify Cloud
          └─ Fail 3x → Start AP mode (blinking LED)
                       └─ Wait portal config
                       └─ Got SSID/Password → Save NVS, restart
```

### 7.3 Cloud Sync Flow
```
Wi-Fi Connected → Cloud Connect
                  ├─ Auth → Get session token
                  ├─ Report device state (heartbeat)
                  ├─ Listen for commands
                  │  ├─ Relay on/off → Execute & report
                  │  ├─ Config change → Update & save NVS
                  │  └─ Request sensor → Read & report
                  └─ On disconnect → Retry with backoff
```

### 7.4 OTA Update Flow
```
Cloud reports new FW version → Device checks version
                               ├─ Match → No action
                               └─ Newer → Start OTA
                                   ├─ Download from URL
                                   ├─ Verify SHA256/signature
                                   ├─ Write to OTA partition
                                   ├─ Reboot
                                   └─ If crash → Rollback to old FW
```

### 7.5 Safety Action Flow
```
Sensor reading → Check safety limits
                 ├─ Normal → Continue
                 └─ Violation (e.g., temp high)
                     ├─ Log event
                     ├─ Trigger action (e.g., relay off, shutdown)
                     ├─ Notify cloud
                     └─ Enter safety mode (limited operation)
```

## 8. Dependencies giữa các modules

### 8.1 Dependency Graph
```
Main
├── NVS_Manager (no dependency)
├── Globals (depends on nothing)
├── Wi-Fi (depends on Globals, NVS)
│   └── Wi-Fi Portal (depends on Wi-Fi, DNS)
├── DNS Server (depends on nothing, used by Wi-Fi Portal)
├── Cloud (depends on Wi-Fi, Globals, NVS)
│   ├── Cloud Command (depends on Relay, Sensor, Globals)
│   ├── Cloud Config (depends on NVS)
│   ├── Cloud HTTP (depends on Wi-Fi)
│   └── Cloud Report (depends on Sensor, Relay, Globals)
├── OTA (depends on Wi-Fi, Cloud, NVS)
│   └── OTA Firebase (depends on Cloud HTTP)
├── Sensor (depends on Globals, no external connectivity)
├── Relay (depends on Globals, NVS, Safety)
├── UI (depends on Globals)
└── Safety (depends on Sensor, Relay, Globals)
```

### 8.2 Critical Dependencies (không thể bỏ qua)
- **Wi-Fi trước Cloud**: Cloud cần Wi-Fi để kết nối
- **NVS trước Cloud**: Cloud cần credentials từ NVS
- **Globals trước tất cả**: Hầu hết module dùng shared state
- **Sensor trước Safety**: Safety cần dữ liệu cảm biến để quyết định
- **Safety trước Relay**: Relay phải kiểm tra điều kiện an toàn trước khi hoạt động

## 9. API công khai chính của từng module

### 9.1 Wi-Fi Module
```c
esp_err_t wifi_init(void);           // Khởi tạo Wi-Fi subsystem
esp_err_t wifi_connect(void);        // Kết nối SSID đã lưu hoặc start AP
void wifi_stop(void);                // Dừng Wi-Fi
int wifi_get_status(void);           // 0=disconnected, 1=connecting, 2=connected
const char* wifi_get_ssid(void);     // Lấy SSID hiện tại
```

### 9.2 Cloud Module
```c
esp_err_t cloud_init(void);          // Khởi tạo cloud subsystem
esp_err_t cloud_connect(void);       // Kết nối cloud server
void cloud_disconnect(void);         // Ngắt kết nối
int cloud_get_status(void);          // 0=disconnected, 1=connecting, 2=connected
esp_err_t cloud_send_report(cloud_report_t *report);  // Gửi report
```

### 9.3 OTA Module
```c
esp_err_t ota_init(void);            // Khởi tạo OTA subsystem
esp_err_t ota_start_update(const char *url);  // Bắt đầu OTA từ URL
int ota_get_progress(void);          // Lấy tiến độ (0-100%)
```

### 9.4 Relay Module
```c
esp_err_t relay_init(void);          // Khởi tạo relay pins
esp_err_t relay_set(int relay_id, int state);  // Bật/tắt relay (0/1)
int relay_get(int relay_id);         // Lấy trạng thái relay
```

### 9.5 Sensor Module
```c
esp_err_t sensor_init(void);         // Khởi tạo ADC/UART/I2C
float sensor_read(int sensor_id);    // Đọc giá trị cảm biến
esp_err_t sensor_calibrate(int sensor_id, float offset);
```

### 9.6 UI Module
```c
esp_err_t ui_init(void);             // Khởi tạo LED/buzzer
void ui_set_state(int state);        // Đặt LED state (0=off, 1=on, 2=blink)
void ui_beep(int duration_ms);       // Kêu buzzer
```

### 9.7 Safety Module
```c
esp_err_t safety_init(void);         // Khởi tạo safety subsystem
int safety_check(void);              // Kiểm tra điều kiện, return 0=safe, 1=violation
void safety_trigger_action(int action_id);  // Thực thi hành động an toàn
```

### 9.8 NVS Manager
```c
esp_err_t nvs_init(void);            // Khởi tạo NVS
esp_err_t nvs_set_string(const char *key, const char *value);
esp_err_t nvs_get_string(const char *key, char *out, size_t max_len);
esp_err_t nvs_set_int32(const char *key, int32_t value);
esp_err_t nvs_get_int32(const char *key, int32_t *out);
```

### 9.9 Globals / Global State
```c
// Defined in globals.h
extern global_state_t g_state;       // Main state struct
extern SemaphoreHandle_t g_state_mutex;  // Protection for g_state

// Helper macros
#define LOCK_STATE()    xSemaphoreTake(g_state_mutex, portMAX_DELAY)
#define UNLOCK_STATE()  xSemaphoreGive(g_state_mutex)
```

## 10. Configuration Details

### 10.1 sdkconfig - Cấu hình ESP-IDF chính
```
# CPU & Memory
CONFIG_IDF_TARGET=esp32           # Vi điều khiển target
CONFIG_ESP32_SPIRAM_SUPPORT=y      # External PSRAM nếu có

# Wi-Fi
CONFIG_ESP_WIFI_SSID=""            # SSID mặc định (rỗng, lấy từ NVS)
CONFIG_ESP_WIFI_PASSWORD=""        # Password mặc định

# Partition & Flash
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_8mb.csv"
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"

# Logging
CONFIG_LOG_DEFAULT_LEVEL=3         # INFO level (tùy chỉnh theo phase)
CONFIG_LOG_COLORS=y                # Colorized output

# OTA
CONFIG_APP_ROLLBACK_ENABLE=y       # Enable rollback on crash
CONFIG_OTA_ALLOW_HTTP=n            # HTTPS only (bảo mật)

# SSL/TLS
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
```

### 10.2 partitions_8mb.csv - Bố cục Flash
```
# Name,     Type, SubType, Offset,    Size,      Flags
nvs,        data, nvs,      0x9000,    0x6000,    
otadata,    data, ota,      0xf000,    0x2000,    
phy_init,   data, phy,      0x11000,   0x1000,    
ota_0,      app,  ota_0,    0x20000,   0x1F0000,  # Partition 0 (1.875 MB)
ota_1,      app,  ota_1,    0x210000,  0x1F0000,  # Partition 1 (1.875 MB)
spiffs,     data, spiffs,   0x400000,  0x400000,  # SPIFFS (4 MB)
```

### 10.3 Timeout & Retry Configuration
```c
// Wi-Fi (trong wifi.c hoặc config.h)
#define WIFI_MAX_RETRY        3
#define WIFI_RETRY_INTERVAL   5000  // ms
#define WIFI_CONNECT_TIMEOUT  15000 // ms

// Cloud (trong cloud_config.c)
#define CLOUD_CONNECT_TIMEOUT    10000  // ms
#define CLOUD_HEARTBEAT_INTERVAL 30000  // ms (report state mỗi 30s)
#define CLOUD_COMMAND_TIMEOUT    5000   // ms

// OTA (trong ota.c)
#define OTA_DOWNLOAD_TIMEOUT     60000  // ms
#define OTA_WRITE_TIMEOUT        30000  // ms

// Sensor (trong sensor.c)
#define SENSOR_READ_INTERVAL     5000   // ms
#define SENSOR_SAMPLE_COUNT      10     // Lấy trung bình 10 mẫu

// Safety (trong safety.c)
#define SAFETY_CHECK_INTERVAL    2000   // ms
#define SAFETY_TEMP_MAX          60.0   // °C
#define SAFETY_TEMP_MIN          0.0    // °C
```

### 10.4 NVS Keys (Key-Value Pair Storage)
```
# Wi-Fi credentials
"wifi_ssid"     → string (max 32 bytes)
"wifi_password" → string (max 64 bytes)

# Cloud config
"cloud_host"    → string (hostname/IP)
"cloud_port"    → int32 (port number, default 8883)
"cloud_client_id" → string (device ID)
"cloud_token"   → string (auth token, max 256 bytes)

# Device config
"device_name"   → string (friendly name)
"relay_mode"    → int32 (mode: 0=manual, 1=auto, 2=schedule)

# OTA
"fw_version"    → string (current firmware version)
"ota_partition" → int32 (0 or 1, which partition is active)

# Safety
"safety_enabled" → int32 (0=disabled, 1=enabled)
"temp_threshold" → float (max safe temperature)
```

## 11. Ghi chú cho agent

### 11.1 Khi cần tìm thông tin
- Bắt đầu từ [main/main.c](main/main.c) để hiểu luồng khởi tạo chung.
- Tìm module theo tên file (ví dụ: wifi → wifi.c + wifi.h).
- Kiểm tra [main/globals.h](main/globals.h) để hiểu cấu trúc trạng thái toàn cục.
- Xem dependency graph (mục 8.1) trước khi thêm/thay đổi module.

### 11.2 Quy trình thay đổi an toàn
1. Hiểu logic module hiện tại.
2. Kiểm tra tất cả call-site của hàm/struct bạn dự tính thay đổi.
3. Kiểm tra dependencies — module này được gọi bởi ai?
4. Viết thay đổi với mô tả rõ ràng.
5. Test toàn bộ chuỗi khởi động + các sự kiện liên quan.
6. Tránh ảnh hưởng đến OTA, NVS, Wi-Fi reconnect.

### 11.3 Ưu tiên giữ tính nhất quán
- Giữ tính nhất quán naming, style, log tag giữa các modules.
- Không thay đổi API public của module mà không cập nhật tất cả call-site.
- Đảm bảo thread-safety khi thêm shared state mới.
- Luôn check/protect access vào shared variables với mutex.

### 11.4 Lưu ý đặc biệt
- **OTA update** là hoạt động quan trọng: kiểm tra kỹ logic rollback, partition management.
- **NVS** có hạn chế số lần ghi: không ghi dữ liệu temporary vào NVS (tối đa ~100k lần).
- **Wi-Fi reconnect** có thể bị timeout: cấu hình timeout phù hợp trong config.h.
- **Cloud communication** phụ thuộc vào Wi-Fi: kiểm tra xử lý lỗi kết nối, retry logic.
- **Safety checks** phải hoạt động trong mọi tình huống: không skip safety logic, luôn check trước relay control.
- **Watchdog timer**: Đảm bảo main loop không bị block lâu, reset watchdog định kỳ.

### 11.5 Debugging Tips
- Kiểm tra log qua serial monitor (115200 baud): `idf.py monitor`
- Log tag theo module giúp lọc thông tin: `idf.py monitor | grep "WiFi"`
- Để xem state toàn cục: thêm hàm debug in g_state (từ globals.h)
- Nếu OTA thất bại: kiểm tra SHA256 checksum, storage space, rollback flag
- Nếu Wi-Fi mất: kiểm tra reconnect logic, AP fallback, portal availability
