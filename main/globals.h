/**
 * @file globals.h
 * @brief Khai báo các biến toàn cục, Queue, Mutex và hàm quản lý phiên sạc.
 *
 * Module này là "trung tâm dữ liệu" của hệ thống, được include bởi tất cả
 * các module khác cần truy cập trạng thái hệ thống hoặc dữ liệu cảm biến.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "types.h"
#include "config.h"
#include "globals_session.h"  // Session lifecycle API (re-exported)

// BIẾN TOÀN CỤC - TRẠNG THÁI HỆ THỐNG
/**
 * @brief Trạng thái hiện tại của State Machine hệ thống.
 *
 * Biến volatile để đảm bảo các task trên cả hai core đều đọc
 * giá trị mới nhất từ bộ nhớ thay vì cache thanh ghi.
 *
 * @warning Mọi ghi vào biến này nên đi qua globals_set_system_state() để giảm
 *          ghi đè chéo giữa các task và tập trung việc đồng bộ tại một chỗ.
 */
extern volatile system_state_t  g_system_state;

/**
 * @brief Mã lỗi hiện tại (NONE nếu không có lỗi).
 */
extern volatile error_code_t    g_error_code;

/**
 * @brief Trạng thái kết nối mạng.
 */
extern volatile network_status_t g_network_status;

/**
 * @brief Giới hạn dòng điện có thể bị thay đổi qua lệnh UPDATE_CONFIG.
 */
extern volatile float           g_max_current_limit;

/**
 * @brief Thông tin phiên sạc đang diễn ra.
 * Bảo vệ bởi session_mutex khi đọc/ghi.
 */
extern charging_session_t       g_current_session;

/**
 * @brief Snapshot dữ liệu cảm biến mới nhất (để cloud task đọc và publish).
 * Bảo vệ bởi sensor_data_mutex.
 */
extern pzem_data_t              g_latest_sensor_data;

/**
 * @brief Thời gian hệ thống chạy (giây). Dùng cho heartbeat.
 */
extern volatile uint32_t        g_uptime_seconds;

// FREERTOS PRIMITIVES
/**
 * @brief Queue truyền dữ liệu cảm biến từ sensor_task sang safety_task.
 *
 * Producer: sensor_task (Core 1)
 * Consumer: safety_task (Core 1)
 * Capacity: 1 phần tử, luôn giữ sample mới nhất để safety_task xử lý nhanh nhất
 */
extern QueueHandle_t            g_sensor_queue;

/**
 * @brief Mutex bảo vệ g_current_session (đọc/ghi từ nhiều task).
 */
extern SemaphoreHandle_t        g_session_mutex;

/**
 * @brief Mutex bảo vệ g_latest_sensor_data.
 */
extern SemaphoreHandle_t        g_sensor_data_mutex;

/**
 * @brief Mutex bảo vệ g_system_state khi cần read-modify-write.
 */
extern SemaphoreHandle_t        g_state_mutex;

/**
 * @brief Event Group cho việc đồng bộ khởi động.
 * Bit 0: WiFi connected, Bit 1: Firebase reachable
 */
extern EventGroupHandle_t       g_network_event_group;

#define NETWORK_WIFI_CONNECTED_BIT  BIT0
#define NETWORK_FIREBASE_OK_BIT     BIT1

// HÀM KHỞI TẠO
/**
 * @brief Khởi tạo tất cả biến toàn cục, Queue và Mutex.
 *
 * Phải được gọi đầu tiên trong app_main() trước tất cả module khác.
 */
void globals_init(void);

/**
 * @brief Ghi system state theo cách có đồng bộ mutex khi khả dụng.
 *
 * Dùng helper này thay cho g_system_state = ... ở các task/module khác nhau.
 */
void globals_set_system_state(system_state_t new_state);

/**
 * @brief Đọc system state hiện tại (đồng bộ qua g_state_mutex khi khả dụng).
 *
 * Dùng getter thay vì đọc trực tiếp g_system_state từ task/UI để khớp với globals_set_system_state().
 */
system_state_t globals_get_system_state(void);

/**
 * @brief Ghi mã lỗi hệ thống theo cách có đồng bộ mutex khi khả dụng.
 */
void globals_set_error_code(error_code_t new_error_code);

/**
 * @brief Đọc mã lỗi hệ thống hiện tại.
 */
error_code_t globals_get_error_code(void);

/**
 * @brief Ghi trạng thái kết nối mạng theo cách có đồng bộ mutex khi khả dụng.
 */
void globals_set_network_status(network_status_t new_status);

/**
 * @brief Đọc trạng thái kết nối mạng hiện tại.
 */
network_status_t globals_get_network_status(void);

/**
 * @brief Ghi giới hạn dòng tối đa (thread-safe, bảo vệ bởi g_state_mutex).
 */
void globals_set_max_current_limit(float new_limit);

/**
 * @brief Đọc giới hạn dòng tối đa hiện tại (thread-safe).
 */
float globals_get_max_current_limit(void);

// HÀM QUẢN LÝ PHIÊN SẠC
// (Đã chuyển sang globals_session.h, được re-export qua include ở đầu file.)

// HÀM QUẢN LÝ DỮ LIỆU CẢM BIẾN
/**
 * @brief Cập nhật dữ liệu cảm biến mới nhất (thread-safe).
 *
 * @param data Dữ liệu PZEM mới đọc được
 */
void globals_update_sensor_data(const pzem_data_t *data);

/**
 * @brief Lấy snapshot dữ liệu cảm biến mới nhất (thread-safe).
 *
 * @param[out] out_data Buffer để ghi snapshot vào
 */
void globals_get_sensor_data(pzem_data_t *out_data);

/**
 * @brief Chuyển đổi system_state_t sang chuỗi để log và Firebase.
 *
 * @param state Trạng thái cần chuyển
 * @return String literal mô tả trạng thái
 */
const char *system_state_to_str(system_state_t state);
