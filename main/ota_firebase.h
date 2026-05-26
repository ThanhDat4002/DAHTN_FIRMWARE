/**
 * @file ota_firebase.h
 * @brief Lớp truy cập Firebase cho module OTA: metadata GET/PATCH, publish state.
 *
 * Header NỘI BỘ - chỉ include bởi ota.c / ota_firebase.c.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define OTA_STATION_PATH_MAX_LEN  96

/**
 * @brief Đọc Firebase config (hardcoded + NVS override) cho OTA module.
 *
 * Idempotent - lần đầu load và cache; các lần sau no-op nếu đã loaded
 * (trừ khi gọi lại để refresh sau khi cấu hình NVS đổi).
 */
void ota_firebase_load_config(void);

/**
 * @brief TRUE nếu đã có đủ Firebase URL và device_id để chạy OTA.
 */
bool ota_firebase_have_config(void);

/**
 * @brief Xây path "/stations/<device_id><suffix>" để dùng với GET/PATCH metadata.
 *
 * @param out_path Buffer ghi path đầy đủ.
 * @param out_len  Kích thước buffer (nên >= OTA_STATION_PATH_MAX_LEN).
 * @param suffix   Đuôi sau device_id, e.g. "/ota", "/ota/firmware_url".
 */
void ota_firebase_build_station_path(char *out_path, size_t out_len, const char *suffix);

/**
 * @brief GET một giá trị string từ Firebase path.
 *
 * @param path    Path đầy đủ (đã build qua ota_firebase_build_station_path).
 * @param out     Buffer nhận giá trị.
 * @param out_len Kích thước buffer.
 * @return ESP_OK, ESP_ERR_NOT_FOUND (giá trị null), ESP_ERR_INVALID_RESPONSE, hoặc mã lỗi HTTP.
 */
esp_err_t ota_firebase_get_string(const char *path, char *out, size_t out_len);

/**
 * @brief Publish trạng thái OTA lên Firebase tại "/stations/<id>/ota".
 *
 * Có dedup nội bộ (cùng status + progress + error => no-op) và rate-limit.
 *
 * @param status      "downloading" | "flashing" | "success" | "error" | "ready"
 * @param progress    0..100 (-1 nếu không áp dụng)
 * @param error_text  NULL hoặc chuỗi mô tả lỗi
 */
void ota_firebase_publish_state(const char *status, int progress, const char *error_text);
