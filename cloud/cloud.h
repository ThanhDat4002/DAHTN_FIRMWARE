
#pragma once
#include "types.h"


void cloud_init(void);

/**
 * @brief Nhiệm vụ định kỳ đẩy telemetry và trạng thái lên Firebase.
 *
 * Chạy trên Core 0, chu kỳ CLOUD_PUBLISH_PERIOD_MS (5s).
 *
 * @param pvParameters Không dùng (NULL)
 */
void cloud_publish_task(void *pvParameters);

/**
 * @brief Nhiệm vụ định kỳ đọc lệnh từ Firebase xuống.
 *
 * Chạy trên Core 0, chu kỳ CLOUD_POLL_PERIOD_MS (1s).
 *
 * @param pvParameters Không dùng (NULL)
 */
void cloud_poll_task(void *pvParameters);

/**
 * @brief Báo cáo kết thúc phiên sạc lên backend web.
 *
 * Dùng khi session kết thúc do người dùng, lỗi trạm hoặc auto-stop.
 */
bool cloud_report_session_end(stop_reason_t reason, charge_result_status_t result_status);
