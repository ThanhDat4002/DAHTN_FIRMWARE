#pragma once

#include "sdkconfig.h"

// ==================== Firmware ====================
#define FIRMWARE_VERSION              "v1.0.7"    // Phiên bản firmware hiện tại

// ==================== GPIO Mapping ====================
#define RELAY_PIN                     32          // Chân điều khiển relay
#define BUZZER_PIN                    13          // Chân buzzer
#define LED_STATUS_PIN                23          // LED trạng thái chung
#define LED_CHARGING_PIN              22          // LED báo đang sạc
#define LED_FAULT_PIN                 21          // LED báo lỗi
#define WIFI_RESET_BUTTON_PIN         33          // Nút reset Wi-Fi
#define WIFI_RESET_BUTTON_HOLD_MS     2000UL      // Thời gian giữ nút reset Wi-Fi (ms)
#define BOOT_BUTTON_PIN               0           // Nút BOOT
#define MASTER_RESET_HOLD_MS          3000UL      // Thời gian giữ combo để master reset (ms)

// ==================== PZEM-004T (UART) ====================
#define PZEM_UART_NUM                 UART_NUM_2  // UART dùng cho PZEM
#define PZEM_TXD_PIN                  17          // TX ESP32 -> RX PZEM
#define PZEM_RXD_PIN                  16          // RX ESP32 <- TX PZEM
#define PZEM_UART_BAUD                9600        // Baudrate UART PZEM
#define PZEM_SLAVE_ADDR               0x01        // Địa chỉ Modbus slave
#define PZEM_BUF_SIZE                 256         // Kích thước buffer UART

#define SENSOR_BOOT_GRACE_MS          5000UL      // Thời gian bỏ qua lỗi sensor sau khi bật relay (ms)
#define SENSOR_POST_GRACE_MAX_ERRORS  4           // Số mẫu lỗi liên tiếp sau grace để xác nhận lỗi
#define SENSOR_POST_RELAY_OFF_SAMPLE_MS 1500UL    // Đọc thêm sau khi tắt relay để phát hiện relay fault

// ==================== Ngưỡng an toàn điện ====================
#define MAX_CURRENT_A                 1.5f        // Dòng tối đa mặc định (A)
#define REMOTE_MAX_CURRENT_MIN_A      0.1f        // Giới hạn dưới cho maxCurrent từ cloud (A)
#define REMOTE_MAX_CURRENT_MAX_A      32.0f       // Giới hạn trên cho maxCurrent từ cloud (A)
#define MAX_VOLTAGE_V                 380.0f      // Ngưỡng quá áp (V)
#define MIN_VOLTAGE_V                 180.0f      // Ngưỡng thấp áp (V)
#define OVER_VOLTAGE_CONFIRM_SAMPLES  3           // Số mẫu liên tiếp để xác nhận quá áp
#define UNDER_VOLTAGE_CONFIRM_SAMPLES 3           // Số mẫu liên tiếp để xác nhận thấp áp
#define OVER_CURRENT_CONFIRM_SAMPLES  1           // Số mẫu liên tiếp để xác nhận quá dòng
#define ZERO_CURRENT_EPS_A            0.001f      // Ngưỡng coi như dòng bằng 0 (A)
#define TRICKLE_CURRENT_A             0.0015f     // Ngưỡng dòng trickle để tự ngắt (A)
#define AUTO_CUTOFF_MS                300000UL    // Thời gian trickle để kết luận đầy pin (ms)
#define UNPLUG_DETECT_MS              2000UL      // Thời gian xác nhận rút súng sạc (ms)
#define UNPLUG_ARM_CURRENT_A          0.05f       // Dòng tối thiểu để "arm" chống rút sạc (A)
#define UNPLUG_ARM_MIN_POWER_W        5.0f        // Công suất tối thiểu để "arm" chống rút sạc (W)
#define UNPLUG_ARM_CONFIRM_SAMPLES    3           // Số mẫu liên tiếp để arm chống rút sạc

// ==================== Wi-Fi ====================
#define WIFI_AP_SSID                  "EVION_Setup"  // SSID AP captive portal
#define WIFI_AP_PASSWORD              ""             // Mật khẩu AP (rỗng = open)
#define WIFI_AP_CHANNEL               1              // Kênh Wi-Fi AP
#define WIFI_AP_MAX_CONN              4              // Số client tối đa kết nối AP
#define WIFI_AP_IP                    "192.168.4.1" // IP AP captive portal
#define WIFI_MAX_RETRY                5              // Số lần retry trước khi xét fallback provisioning
#define WIFI_RECONNECT_BASE_MS        2000UL         // Backoff reconnect ban đầu (ms)
#define WIFI_RECONNECT_MAX_MS         60000UL        // Backoff reconnect tối đa (ms)
#define WIFI_MANAGER_TICK_MS          1000UL         // Chu kỳ task quản lý mạng (ms)
#define WIFI_PROVISIONING_RECOVERY_MS 120000UL       // Mất kết nối quá lâu thì fallback provisioning (ms)

// ==================== Cloud / Firebase ====================
#define FIREBASE_DATABASE_URL         "https://tramsacmini-default-rtdb.firebaseio.com" // URL RTDB
#define FIREBASE_AUTH_TOKEN           ""                                                  // Token REST tùy chọn
#define FIREBASE_DEVICE_ID            "station_01"                                        // Device ID mặc định
#define BACKEND_API_BASE_URL          "https://tramsacmini.web.app"                       // Backend API base URL

#define FIREBASE_HTTP_TIMEOUT_MS      5000        // Timeout HTTP cloud (ms)
#define MAX_COMMAND_ID_LEN            64          // Độ dài tối đa command ID
#define MAX_ISSUED_BY_LEN             64          // Độ dài tối đa trường issued_by

// ==================== Chu kỳ task ====================
#define SENSOR_TASK_PERIOD_MS         200         // Chu kỳ đọc sensor (ms)
#define CLOUD_PUBLISH_PERIOD_MS       5000        // Chu kỳ publish telemetry lên cloud (ms)
#define CLOUD_POLL_PERIOD_MS          1000        // Chu kỳ poll command/config từ cloud (ms)
#define HEARTBEAT_PERIOD_MS           5000        // Chu kỳ heartbeat (ms)
#define NVS_SESSION_SAVE_PERIOD_MS    30000       // Chu kỳ lưu session vào NVS khi đang sạc (ms)

// ==================== Stack size task (bytes) ====================
#define SENSOR_TASK_STACK             4096        // Stack sensor_task
#define SAFETY_TASK_STACK             8192        // Stack safety_task
#define UI_TASK_STACK                 2048        // Stack ui_task
#define BUTTON_TASK_STACK             2048        // Stack wifi_reset_button_task
#define CLOUD_PUBLISH_STACK           8192        // Stack cloud_publish_task
#define CLOUD_POLL_STACK              8192        // Stack cloud_poll_task
#define NETWORK_TASK_STACK            4096        // Stack network_task

// ==================== Priority task ====================
#define PRIORITY_SAFETY               6           // Cao nhất: bảo vệ an toàn
#define PRIORITY_SENSOR               5           // Đọc cảm biến
#define PRIORITY_CLOUD                4           // Cloud I/O
#define PRIORITY_NETWORK              3           // Quản lý Wi-Fi/reconnect
#define PRIORITY_UI                   2           // UI LED/Buzzer
#define PRIORITY_BUTTON               1           // Quét nút reset

// ==================== Core pinning ====================
#define CORE_HARDWARE                 1           // Core 1: Sensor, Safety, UI
#define CORE_NETWORK                  0           // Core 0: Cloud, Wi-Fi, OTA

// ==================== NVS namespace / key ====================
#define NVS_NAMESPACE_WIFI            "wifi_cfg"       // Namespace lưu Wi-Fi
#define NVS_KEY_SSID                  "ssid"           // Key SSID
#define NVS_KEY_PASSWORD              "password"       // Key mật khẩu Wi-Fi

#define NVS_NAMESPACE_FIREBASE        "firebase_cfg"   // Namespace lưu cloud config
#define NVS_KEY_FB_URL                "fb_url"         // Key Firebase URL
#define NVS_KEY_FB_TOKEN              "fb_token"       // Key Firebase token
#define NVS_KEY_DEVICE_ID             "device_id"      // Key device ID
#define NVS_KEY_RUNTIME_MAX_CUR       "runtime_max_cur"// Key maxCurrent runtime

#define NVS_NAMESPACE_SESSION         "session"        // Namespace lưu session sạc
#define NVS_KEY_SESSION_ACTIVE        "active"         // Cờ session active
#define NVS_KEY_SESSION_ID            "sess_id"        // ID session
#define NVS_KEY_SESSION_USER          "user_id"        // ID người dùng
#define NVS_KEY_SESSION_START         "start_ts"       // Timestamp bắt đầu
#define NVS_KEY_SESSION_END           "end_ts"         // Timestamp kết thúc
#define NVS_KEY_SESSION_ENERGY        "energy_wh"      // Năng lượng session (Wh)
#define NVS_KEY_SESSION_TARGET        "target_wh"      // Mục tiêu năng lượng (Wh)
#define NVS_KEY_SESSION_REASON        "end_reason"     // Lý do kết thúc
#define NVS_KEY_SESSION_MAX_CUR       "max_current"    // Giới hạn dòng của session

#define NVS_NAMESPACE_FAULT           "fault"          // Namespace lưu fault latch
#define NVS_KEY_FAULT_LATCHED         "latched"        // Cờ fault đang bị latch
#define NVS_KEY_FAULT_CODE            "code"           // Mã lỗi đã latch

// ==================== Kích thước buffer ====================
#define MAX_SSID_LEN                  32          // Độ dài tối đa SSID
#define MAX_PASSWORD_LEN              64          // Độ dài tối đa mật khẩu Wi-Fi
#define MAX_URL_LEN                   1024        // Độ dài tối đa URL
#define MAX_TOKEN_LEN                 256         // Độ dài tối đa token
#define MAX_DEVICE_ID_LEN             32          // Độ dài tối đa device ID
#define MAX_SESSION_ID_LEN            32          // Độ dài tối đa session ID
#define MAX_JSON_BUF_SIZE             512         // Buffer JSON nội bộ
#define MAX_HTTP_RESPONSE_SIZE        4096        // Buffer nhận HTTP response
#define WATCHDOG_TIMEOUT_S            3           // Timeout watchdog (giây)
