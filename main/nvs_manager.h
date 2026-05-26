/**
 * @file nvs_manager.h
 * @brief Giao diện quản lý NVS Flash — lưu/đọc cấu hình và phục hồi phiên sạc.
 *
 * Module này tập trung tất cả thao tác với NVS Flash:
 *  - Cấu hình WiFi (nhập qua Captive Portal)
 *  - Cấu hình cloud/runtime (firebase_url, firebase_token, device_id)
 *  - Lưu định kỳ trạng thái phiên sạc để phục hồi sau mất điện
 */

#pragma once

#include <stdbool.h>
#include "types.h"

/**
 * @brief Khởi tạo NVS Flash.
 *
 * Phải được gọi trước tất cả hàm nvs_* khác.
 * Nếu NVS bị hỏng sẽ tự động xóa và format lại.
 */
void nvs_manager_init(void);

// WIFI CONFIG
/**
 * @brief Lưu cấu hình WiFi vào NVS Flash.
 *
 * @param config Struct chứa SSID và password WiFi
 * @return true nếu lưu thành công
 */
bool nvs_save_wifi_config(const wifi_config_data_t *config);

/**
 * @brief Đọc cấu hình WiFi từ NVS Flash.
 *
 * @param[out] config Buffer để ghi dữ liệu đọc được
 * @return true nếu tìm thấy cấu hình hợp lệ, false nếu chưa có cấu hình
 */
bool nvs_load_wifi_config(wifi_config_data_t *config);

/**
 * @brief Xóa toàn bộ cấu hình WiFi khỏi NVS Flash.
 *
 * Dùng khi người dùng muốn bắt đầu cấu hình lại từ đầu.
 */
void nvs_clear_wifi_config(void);


// CLOUD CONFIG
/**
 * @brief Luu cau hinh cloud (firebase_url/firebase_token/device_id) vao NVS.
 *
 * @param config Struct chua cau hinh cloud can luu
 * @return true neu luu thanh cong
 */
bool nvs_save_cloud_config(const wifi_config_data_t *config);

/**
 * @brief Doc cau hinh cloud (firebase_url/firebase_token/device_id) tu NVS.
 *
 * @param[out] config Buffer de ghi du lieu doc duoc
 * @return true neu co it nhat mot truong cloud hop le trong NVS
 */
bool nvs_load_cloud_config(wifi_config_data_t *config);

/**
 * @brief Luu gioi han dong dien runtime (A) nhan tu Firebase.
 *
 * @param max_current_limit Gia tri dong dien toi da (A), da duoc validate/clamp.
 * @return true neu luu thanh cong
 */
bool nvs_save_runtime_max_current_limit(float max_current_limit);

/**
 * @brief Doc gioi han dong dien runtime (A) da luu trong NVS.
 *
 * @param[out] out_limit Buffer nhan gia tri max current (A)
 * @return true neu doc duoc gia tri hop le
 */
bool nvs_load_runtime_max_current_limit(float *out_limit);

// FAULT LATCH
/**
 * @brief Luu co FAULT latched de giu trang thai su co qua reboot.
 *
 * @param error_code Ma loi khong phai ERR_NONE.
 * @return true neu luu thanh cong
 */
bool nvs_save_fault_latch(error_code_t error_code);

/**
 * @brief Doc thong tin FAULT latched tu NVS.
 *
 * @param[out] out_error_code Ma loi da latch.
 * @return true neu dang co fault latch, false neu khong co.
 */
bool nvs_load_fault_latch(error_code_t *out_error_code);

/**
 * @brief Xoa co FAULT latched khoi NVS.
 */
void nvs_clear_fault_latch(void);

// SESSION RECOVERY
/**
 * @brief Lưu trạng thái phiên sạc hiện tại vào NVS Flash.
 *
 * Được gọi định kỳ mỗi NVS_SESSION_SAVE_PERIOD_MS bởi cloud_task
 * và ngay lập tức khi bắt đầu/kết thúc phiên sạc.
 *
 * @param session Struct phiên sạc cần lưu
 * @return true nếu lưu thành công
 */
bool nvs_save_session(const charging_session_t *session);

/**
 * @brief Đọc phiên sạc đã lưu từ NVS Flash.
 *
 * @param[out] session Buffer để ghi dữ liệu đọc được
 * @return true nếu tồn tại phiên sạc đang dở (active=true trong NVS)
 */
bool nvs_load_session(charging_session_t *session);

/**
 * @brief Xóa dữ liệu phiên sạc khỏi NVS Flash.
 *
 * Được gọi sau khi phiên sạc kết thúc và dữ liệu đã được đồng bộ lên Firebase.
 */
void nvs_clear_session(void);
