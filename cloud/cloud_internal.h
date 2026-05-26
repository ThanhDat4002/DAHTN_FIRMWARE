/**
 * @file cloud_internal.h
 * @brief Header NỘI BỘ duy nhất cho các module cloud_*.c.
 *
 
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "config.h"
#include "types.h"

// 1. HẰNG SỐ DÙNG CHUNG
#define CLOUD_HTTP_RETRY_COUNT 2U
#define CLOUD_HTTP_RETRY_DELAY_MS 500U
#define CLOUD_OFFLINE_FAILURE_THRESHOLD 2U
#define CLOUD_CONFIG_POLL_DIVIDER 3U
#define CLOUD_HTTP_MUTEX_TIMEOUT_MS (FIREBASE_HTTP_TIMEOUT_MS + 500U)
#define CLOUD_CONFIG_CACHE_LEN 1024U

// 2. KIỂU DỮ LIỆU DÙNG CHUNG
/** @brief Header HTTP tùy chọn cho request. */
typedef struct {
    const char *key;
    const char *value;
} cloud_http_header_t;

// 3. HÀM TIỆN ÍCH DÙNG CHUNG (định nghĩa trong cloud.c)
/** @brief Trả về timestamp Unix theo giây (fallback uptime nếu chưa có NTP). */
int64_t cloud_current_timestamp_s(void);

/** @brief Chuyển kWh → Wh, làm tròn về số nguyên không âm. */
int64_t cloud_energy_kwh_to_wh(float energy_kwh);

/** @brief Reset WDT nếu task hiện tại đã subscribe (no-op nếu chưa). */
void cloud_task_wdt_reset_if_subscribed(void);

/** @brief Delay có chia nhỏ để reset WDT trong lúc chờ. */
void cloud_task_delay_with_wdt(uint32_t delay_ms);

/** @brief TRUE nếu lỗi HTTP do mutex bận (không tính là cloud offline). */
bool cloud_request_busy(esp_err_t err);

// 4. HTTP TRANSPORT  (cloud_http.c)
//
// Mọi HTTP request đi qua một mutex chung để serial hoá, tránh nhiều task
// tranh chấp `esp_http_client` cùng lúc.

/** @brief Khởi tạo mutex serial hóa HTTP. Gọi 1 lần trong cloud_init(). */
void cloud_http_init(void);

/**
 * @brief Thực hiện HTTP request đã serial-hóa (lấy mutex, retry, cleanup).
 *
 * @param url           URL đầy đủ.
 * @param method        Method HTTP.
 * @param post_data     Body cho POST/PUT/PATCH (NULL nếu không có).
 * @param resp_buf      Buffer nhận response (NULL nếu không cần).
 * @param resp_len      Kích thước resp_buf.
 * @param headers       Mảng header tùy chọn (NULL nếu không có).
 * @param header_count  Số phần tử headers.
 * @return ESP_OK nếu thành công, hoặc mã lỗi tương ứng.
 */
esp_err_t cloud_http_request(const char *url,
                             esp_http_client_method_t method,
                             const char *post_data,
                             char *resp_buf,
                             size_t resp_len,
                             const cloud_http_header_t *headers,
                             size_t header_count);

/** @brief GET node Firebase tại path tương đối (e.g. "/command"). */
esp_err_t cloud_http_firebase_get(const char *path, char *resp_buf, size_t resp_len);

/** @brief PATCH JSON lên node Firebase tại path tương đối. */
esp_err_t cloud_http_firebase_patch(const char *path, const char *json_payload);

/** @brief PUT JSON nguyên block lên node Firebase. */
esp_err_t cloud_http_firebase_put_json(const char *path, const char *json_payload);

/** @brief PUT một chuỗi (đã bọc dấu nháy JSON) lên node Firebase. */
esp_err_t cloud_http_firebase_put_string(const char *path, const char *value);

// 5. CONFIG + NTP + ONLINE STATE  (cloud_config.c)
/**
 * @brief Đọc Firebase config (hardcoded + NVS override) và đánh dấu sẵn sàng.
 *
 * @return TRUE nếu có đủ Firebase URL + device_id để chạy cloud.
 */
bool cloud_config_init(void);

/** @brief TRUE nếu Firebase config đã sẵn sàng (cloud_init đã chạy thành công). */
bool cloud_config_has_config(void);

/** @brief Lấy snapshot Firebase config hiện tại (read-only). */
const wifi_config_data_t *cloud_config_get(void);

/**
 * @brief Xây URL đầy đủ cho Firebase REST API.
 *
 * Tự động thêm `/stations/<device_id>` + suffix path + `.json` + auth token.
 *
 * @param out_url  Buffer ghi URL.
 * @param out_len  Kích thước buffer.
 * @param path     Path tương đối bên dưới `/stations/<device_id>` (e.g. "/command", "/ota").
 */
void cloud_config_build_url(char *out_url, size_t out_len, const char *path);

/**
 * @brief Đồng bộ thời gian qua SNTP (idempotent, có cache).
 *
 * @return TRUE nếu thời gian đã hợp lệ (đã từng sync OK).
 */
bool cloud_config_sync_ntp(void);

/**
 * @brief Cập nhật trạng thái cloud online/offline (có debounce).
 *
 * Tự cập nhật `g_system_state` và `g_network_event_group`.
 */
void cloud_config_set_online(bool online);

// 6. COMMAND PROCESSING  (cloud_command.c)
/**
 * @brief Xử lý JSON response từ node `/command` của Firebase.
 *
 * Tự parse JSON (chuỗi hoặc object), kiểm tra duplicate / expire,
 * gọi handler tương ứng (START / STOP / RESET / OTA / UPDATE_CONFIG),
 * ghi nhớ command_id để chống xử lý lặp.
 *
 * Hàm này tự log và xử lý lỗi nội bộ; KHÔNG ném ra ngoài.
 *
 * @param resp_json Chuỗi JSON từ Firebase (NULL/"" => no-op).
 */
void cloud_command_process_response(const char *resp_json);

/**
 * @brief Xử lý JSON response từ node `/config` của Firebase.
 *
 * Có dedup theo payload thô để tránh parse lại khi config không đổi.
 *
 * @param resp_json Chuỗi JSON từ Firebase (NULL/"" / "null" => clear cache).
 */
void cloud_command_apply_config_response(const char *resp_json);

// 7. SESSION REPORT  (cloud_report.c)
//
// Public API `cloud_report_session_end` được khai báo trong cloud.h.

/**
 * @brief Kiểm tra và gửi report cho session vừa kết thúc (nếu chưa report).
 *
 * Dùng trong cloud_publish_task mỗi chu kỳ — tự lấy snapshot session,
 * kiểm tra điều kiện, suy ra result_status từ end_reason rồi gọi
 * cloud_report_session_end().
 */
void cloud_report_check_pending(void);

/**
 * @brief Xóa session_id đã report gần nhất.
 *
 * Gọi khi bắt đầu một session mới (CMD_START_CHARGING) để cho phép report
 * session mới khi nó kết thúc.
 */
void cloud_report_reset_last_session_id(void);

/** @brief Kiểm tra session đã được report chưa. */
bool cloud_report_was_session_reported(const char *session_id);
