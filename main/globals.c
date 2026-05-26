/**
 * @file globals.c
 * @brief Äá»‹nh nghÄ©a biáº¿n toÃ n cá»¥c, queue, mutex vÃ  cÃ¡c helper state-management.
 *
 * VÃ²ng Ä‘á»i phiÃªn sáº¡c + tÃ­ch phÃ¢n nÄƒng lÆ°á»£ng Ä‘Ã£ Ä‘Æ°á»£c tÃ¡ch sang `globals_session.c`.
 * File nÃ y giá»¯:
 *  - Äá»‹nh nghÄ©a cÃ¡c global state (g_system_state, queues, mutexes, event group)
 *  - HÃ m khá»Ÿi táº¡o `globals_init`
 *  - Getter/setter cho state há»‡ thá»‘ng (state, error code, network status, current limit)
 *  - Quáº£n lÃ½ snapshot dá»¯ liá»‡u cáº£m biáº¿n (publish vÃ o queue + session energy)
 *  - Helper `system_state_to_str`
 */

#include "globals.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#define TAG "GLOBALS"

// Äá»ŠNH NGHÄ¨A BIáº¾N TOÃ€N Cá»¤C
volatile system_state_t     g_system_state          = STATE_BOOT;
volatile error_code_t       g_error_code            = ERR_NONE;
volatile network_status_t   g_network_status        = NET_DISCONNECTED;
volatile float              g_max_current_limit     = MAX_CURRENT_A;
volatile uint32_t           g_uptime_seconds        = 0;

charging_session_t          g_current_session       = { .active = false };
pzem_data_t                 g_latest_sensor_data    = { .valid = false };

QueueHandle_t               g_sensor_queue          = NULL;
SemaphoreHandle_t           g_session_mutex         = NULL;
SemaphoreHandle_t           g_sensor_data_mutex     = NULL;
SemaphoreHandle_t           g_state_mutex           = NULL;
EventGroupHandle_t          g_network_event_group   = NULL;

static pzem_data_t          s_last_sensor_snapshot  = { .valid = false };


// Khởi tạo thành phần trong globals_init.
void globals_init(void) {
    ESP_LOGI(TAG, "Khoi tao du lieu toan cuc...");

    g_sensor_queue = xQueueCreate(1, sizeof(pzem_data_t));
    configASSERT(g_sensor_queue != NULL);

    g_session_mutex      = xSemaphoreCreateMutex();
    g_sensor_data_mutex  = xSemaphoreCreateMutex();
    g_state_mutex        = xSemaphoreCreateMutex();
    configASSERT(g_session_mutex != NULL);
    configASSERT(g_sensor_data_mutex != NULL);
    configASSERT(g_state_mutex != NULL);

    g_network_event_group = xEventGroupCreate();
    configASSERT(g_network_event_group != NULL);

    g_max_current_limit = MAX_CURRENT_A;
    memset(&g_current_session, 0, sizeof(g_current_session));
    memset(&g_latest_sensor_data, 0, sizeof(g_latest_sensor_data));
    memset(&s_last_sensor_snapshot, 0, sizeof(s_last_sensor_snapshot));
    globals_session_init();

    ESP_LOGI(TAG, "Du lieu toan cuc da khoi tao OK.");
}

// STATE GETTERS / SETTERS
// Cập nhật giá trị trong globals_set_system_state.
void globals_set_system_state(system_state_t new_state) {
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_system_state = new_state;
        xSemaphoreGive(g_state_mutex);
    } else {
        g_system_state = new_state;
    }
}
// Lấy dữ liệu trong globals_get_system_state.
system_state_t globals_get_system_state(void) {
    if (g_state_mutex != NULL) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        system_state_t current = g_system_state;
        xSemaphoreGive(g_state_mutex);
        return current;
    }
    return g_system_state;
}
// Cập nhật giá trị trong globals_set_error_code.
void globals_set_error_code(error_code_t new_error_code) {
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_error_code = new_error_code;
        xSemaphoreGive(g_state_mutex);
    } else {
        g_error_code = new_error_code;
    }
}
// Lấy dữ liệu trong globals_get_error_code.
error_code_t globals_get_error_code(void) {
    if (g_state_mutex != NULL) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        error_code_t current = g_error_code;
        xSemaphoreGive(g_state_mutex);
        return current;
    }
    return g_error_code;
}
// Cập nhật giá trị trong globals_set_network_status.
void globals_set_network_status(network_status_t new_status) {
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_network_status = new_status;
        xSemaphoreGive(g_state_mutex);
    } else {
        g_network_status = new_status;
    }
}
// Lấy dữ liệu trong globals_get_network_status.
network_status_t globals_get_network_status(void) {
    if (g_state_mutex != NULL) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        network_status_t current = g_network_status;
        xSemaphoreGive(g_state_mutex);
        return current;
    }
    return g_network_status;
}
// Cập nhật giá trị trong globals_set_max_current_limit.
void globals_set_max_current_limit(float new_limit) {
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_max_current_limit = new_limit;
        xSemaphoreGive(g_state_mutex);
    } else {
        g_max_current_limit = new_limit;
    }
}
// Lấy dữ liệu trong globals_get_max_current_limit.
float globals_get_max_current_limit(void) {
    if (g_state_mutex != NULL) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        float current = g_max_current_limit;
        xSemaphoreGive(g_state_mutex);
        return current;
    }
    return g_max_current_limit;
}

// SENSOR DATA
// Cập nhật trạng thái trong globals_update_sensor_data.
void globals_update_sensor_data(const pzem_data_t *data) {
    if (xSemaphoreTake(g_sensor_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(&g_latest_sensor_data, data, sizeof(pzem_data_t));
        memcpy(&s_last_sensor_snapshot, data, sizeof(pzem_data_t));
        xSemaphoreGive(g_sensor_data_mutex);
    }

    // Cáº­p nháº­t energy session (khÃ´ng cáº§n mutex riÃªng vÃ¬ Ä‘Ã£ cÃ³ session_mutex bÃªn trong)
    if (data->valid) {
        globals_update_session_energy(data);
    }
}
// Lấy dữ liệu trong globals_get_sensor_data.
void globals_get_sensor_data(pzem_data_t *out_data) {
    if (xSemaphoreTake(g_sensor_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out_data, &g_latest_sensor_data, sizeof(pzem_data_t));
        memcpy(&s_last_sensor_snapshot, &g_latest_sensor_data, sizeof(pzem_data_t));
        xSemaphoreGive(g_sensor_data_mutex);
    } else {
        memcpy(out_data, &s_last_sensor_snapshot, sizeof(pzem_data_t));
    }
}

// HELPERS
const char *system_state_to_str(system_state_t state) {
    switch (state) {
        case STATE_BOOT:     return "BOOT";
        case STATE_IDLE:     return "IDLE";
        case STATE_OFFLINE:  return "OFFLINE";
        case STATE_READY:    return "READY";
        case STATE_CHARGING: return "CHARGING";
        case STATE_STOPPED:  return "STOPPED";
        case STATE_FAULT:    return "FAULT";
        default:             return "UNKNOWN";
    }
}


