/**
 * @file wifi.h
 * @brief WiFi Manager — Phương án 1: Captive Portal (AP + HTTP Web Server).
 *
 * ## Luồng hoạt động 3 bước:
 *
 *  Bước 1 — Đọc NVS: Nếu đã có SSID/Password → thử kết nối STA mode (10s).
 *            Thành công → vào READY. Thất bại → chuyển bước 2.
 *
 *  Bước 2 — AP Mode: ESP32 phát WiFi tên "EVION_Setup" (không cần mật khẩu).
 *            Khởi động HTTP Server trên 192.168.4.1.
 *            Mọi DNS request → redirect về 192.168.4.1 (Captive Portal trigger).
 *
 *  Bước 3 — Nhận config: User mở trình duyệt → điền SSID + Password
 *            → Submit → ESP lưu NVS → Restart.
 *
 *  Device ID có thể lưu riêng trong NVS để dùng chung firmware cho nhiều trạm.
 *
 * ## Sau khi online:
 *  - Auto-reconnect với Exponential Backoff khi mất WiFi.
 *  - Offline Mode: g_system_state → STATE_OFFLINE (relay vẫn đóng nếu đang sạc).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Khởi tạo WiFi và bắt đầu luồng kết nối.
 *
 * Hàm này chỉ chuẩn bị hạ tầng WiFi và khởi động luồng STA/AP cần thiết.
 * Nó không block chờ user nhập captive portal nữa để app_main vẫn có thể
 * khôi phục phiên sạc dang dở ngay cả khi mạng chưa sẵn sàng.
 *
 * Phải được gọi sau nvs_manager_init() và trước khi tạo các task.
 *
 * @return true nếu đã có WiFi config và đã khởi động kết nối STA nền
 * @return false nếu chưa có WiFi config, hoặc đang ở chế độ captive portal/offline
 */
bool wifi_init(void);

/**
 * @brief Lấy địa chỉ IP hiện tại dưới dạng chuỗi.
 *
 * @param[out] buf    Buffer để ghi địa chỉ IP
 * @param[in]  buflen Kích thước buffer (nên >= 16)
 */
void wifi_get_ip(char *buf, size_t buflen);

/**
 * @brief Kiểm tra WiFi có đang kết nối không.
 *
 * @return true nếu đang online
 */
bool wifi_is_connected(void);

/**
 * @brief Task giám sát nút reset WiFi.
 *
 * Nhấn giữ nút được cấu hình ở WIFI_RESET_BUTTON_PIN để xóa WiFi config
 * và khởi động lại vào chế độ captive portal.
 */
void wifi_reset_button_task(void *pvParameters);

/**
 * @brief Network Task — tự động reconnect WiFi với Exponential Backoff.
 *
 * Chạy trên Core 0 (CORE_NETWORK), Priority PRIORITY_NETWORK.
 * Khi phát hiện mất kết nối:
 *   - Chuyển STATE_CHARGING → STATE_OFFLINE (relay vẫn đóng)
 *   - Chuyển STATE_READY → STATE_IDLE
 *   - Thực hiện retry với thời gian chờ tăng dần (2s → 4s → 8s → ... → 60s)
 *
 * @param pvParameters Không dùng (NULL)
 */
void network_task(void *pvParameters);
