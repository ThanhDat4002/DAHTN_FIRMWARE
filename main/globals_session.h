
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "types.h"

/**
 * @brief Reset state nội bộ của module session (gọi từ globals_init).
 *
 * Xóa snapshot session gần nhất và state tích phân năng lượng.
 */
void globals_session_init(void);

/**
 * @brief Bắt đầu một phiên sạc mới.
 *
 * @param command   Lệnh START_CHARGING chứa session_id và user_id
 * @param message   Buffer thông báo lỗi (nếu thất bại)
 * @param msg_size  Kích thước buffer message
 * @return true nếu tạo phiên thành công, false nếu đã có phiên đang chạy
 */
bool globals_begin_session(const station_command_t *command, char *message, size_t msg_size);

/**
 * @brief Kết thúc phiên sạc hiện tại và chốt dữ liệu.
 *
 * @param reason        Lý do dừng sạc
 * @param message       Buffer thông báo
 * @param msg_size      Kích thước buffer
 * @return true nếu có phiên để kết thúc, false nếu không có phiên nào
 */
bool globals_finish_session(stop_reason_t reason,
                            char *message,
                            size_t msg_size);

/**
 * @brief Cập nhật điện năng tiêu thụ trong phiên hiện tại.
 *
 * Chuẩn nội bộ dùng kWh.
 * Tính theo công suất tức thời (W), tích phân theo từng giây:
 *   kWh += power(W) * 1s / 3,600,000
 *
 * @param sample Mẫu dữ liệu sensor mới nhất
 */
void globals_update_session_energy(const pzem_data_t *sample);

/**
 * @brief Cập nhật giới hạn dòng tối đa đang áp dụng cho phiên sạc hiện tại.
 */
void globals_set_active_session_max_current_limit(float max_current_limit);

/**
 * @brief Kiểm tra nhanh có phiên sạc active hay không.
 */
bool globals_is_session_active(void);

/**
 * @brief Lấy snapshot phiên sạc hiện tại (thread-safe).
 *
 * @param[out] out_session Buffer để ghi snapshot vào
 */
void globals_get_session_snapshot(charging_session_t *out_session);
