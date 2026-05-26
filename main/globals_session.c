/**
 * @file globals_session.c
 * @brief Triá»ƒn khai vÃ²ng Ä‘á»i phiÃªn sáº¡c + tÃ­ch phÃ¢n nÄƒng lÆ°á»£ng.
 *
 * Truy cáº­p `g_current_session` (Ä‘á»‹nh nghÄ©a trong globals.c) Ä‘Ã£ Ä‘Æ°á»£c serial hÃ³a
 * báº±ng `g_session_mutex`. State cá»§a bá»™ tÃ­ch phÃ¢n nÄƒng lÆ°á»£ng Ä‘Æ°á»£c giá»¯ private
 * trong file nÃ y.
 */

#include "globals_session.h"
#include "globals.h"
#include "nvs_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "SESSION"
#define ENERGY_INTEGRATION_STEP_MS 1000U

// PRIVATE STATE
static charging_session_t s_last_session_snapshot           = { .active = false };
static float              s_energy_integrator_power_w       = 0.0f;
static uint32_t           s_energy_integrator_last_ts_ms    = 0U;
static uint32_t           s_energy_integrator_residual_ms   = 0U;

// LOCAL HELPERS
// Thực hiện xử lý trong current_timestamp_s.
static int64_t current_timestamp_s(void) {
    time_t now = time(NULL);
    if (now > 946684800) {
        return (int64_t)now;
    }
    return (int64_t)(esp_timer_get_time() / 1000000LL);
}
// Thực hiện xử lý trong current_timestamp_ms_u32.
static uint32_t current_timestamp_ms_u32(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}
// Thực hiện xử lý trong refresh_last_session_snapshot_locked.
static void refresh_last_session_snapshot_locked(void) {
    memcpy(&s_last_session_snapshot, &g_current_session, sizeof(s_last_session_snapshot));
}
// Đặt lại trạng thái trong reset_energy_integrator_locked.
static void reset_energy_integrator_locked(float power_w, uint32_t timestamp_ms) {
    s_energy_integrator_power_w     = (power_w > 0.0f) ? power_w : 0.0f;
    s_energy_integrator_last_ts_ms  = timestamp_ms;
    s_energy_integrator_residual_ms = 0U;
}

// INIT (gá»i tá»« globals_init)
// Khởi tạo thành phần trong globals_session_init.
void globals_session_init(void) {
    memset(&s_last_session_snapshot, 0, sizeof(s_last_session_snapshot));
    reset_energy_integrator_locked(0.0f, 0U);
}

// SESSION LIFECYCLE
// Thực hiện xử lý trong globals_begin_session.
bool globals_begin_session(const station_command_t *command, char *message, size_t msg_size) {
    charging_session_t session_snapshot;

    // Äá»c sensor data TRÆ¯á»šC khi láº¥y session_mutex Ä‘á»ƒ trÃ¡nh deadlock
    // (globals_update_sensor_data láº¥y sensor_data_mutex rá»“i session_mutex,
    //  náº¿u ta láº¥y session_mutex trÆ°á»›c rá»“i sensor_data_mutex â†’ deadlock)
    pzem_data_t snap;
    globals_get_sensor_data(&snap);

    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        snprintf(message, msg_size, "Khong the lay session_mutex");
        return false;
    }

    if (g_current_session.active) {
        xSemaphoreGive(g_session_mutex);
        snprintf(message, msg_size, "Da co phien sac dang chay: %s", g_current_session.session_id);
        ESP_LOGW(TAG, "%s", message);
        return false;
    }

    memset(&g_current_session, 0, sizeof(g_current_session));
    g_current_session.active = true;

    strncpy(g_current_session.session_id, command->session_id,
            sizeof(g_current_session.session_id) - 1);
    if (command->user_id[0] != '\0') {
        strncpy(g_current_session.user_id, command->user_id,
                sizeof(g_current_session.user_id) - 1);
    } else {
        strncpy(g_current_session.user_id, command->issued_by,
                sizeof(g_current_session.user_id) - 1);
    }

    g_current_session.start_time = current_timestamp_s();
    g_current_session.end_time   = 0;
    g_current_session.energy_used_kwh = 0.0f;
    g_current_session.end_reason = STOP_REASON_NONE;

    g_current_session.energy_start_kwh = snap.energy_kwh;
    reset_energy_integrator_locked((snap.valid && snap.power > 0.0f) ? snap.power : 0.0f,
                                   (snap.timestamp_ms != 0U) ? snap.timestamp_ms : current_timestamp_ms_u32());

    g_current_session.max_current_limit =
        (command->max_current > 0.0f) ? command->max_current : globals_get_max_current_limit();
    g_current_session.target_energy_kwh =
        (command->target_energy_kwh > 0.0f)
            ? command->target_energy_kwh
            : ((command->target_energy_wh > 0) ? ((float)command->target_energy_wh / 1000.0f) : 0.0f);

    refresh_last_session_snapshot_locked();
    memcpy(&session_snapshot, &g_current_session, sizeof(session_snapshot));

    xSemaphoreGive(g_session_mutex);

    if (!nvs_save_session(&session_snapshot)) {
        ESP_LOGW(TAG, "Khong the luu session moi vao NVS.");
    }

    ESP_LOGI(TAG, "Bat dau phien sac: %s (user: %s)",
             session_snapshot.session_id, session_snapshot.user_id);
    snprintf(message, msg_size, "Phien sac %s da bat dau", command->session_id);
    return true;
}
// Thực hiện xử lý trong globals_finish_session.
bool globals_finish_session(stop_reason_t reason,
                            char *message,
                            size_t msg_size) {
    charging_session_t session_snapshot;

    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        snprintf(message, msg_size, "Khong the lay session_mutex khi ket thuc");
        return false;
    }

    if (!g_current_session.active) {
        xSemaphoreGive(g_session_mutex);
        snprintf(message, msg_size, "Khong co phien sac nao dang chay");
        ESP_LOGW(TAG, "%s", message);
        return false;
    }

    g_current_session.active     = false;
    g_current_session.end_reason = reason;
    g_current_session.end_time   = current_timestamp_s();
    reset_energy_integrator_locked(0.0f, 0U);
    refresh_last_session_snapshot_locked();
    memcpy(&session_snapshot, &g_current_session, sizeof(session_snapshot));

    xSemaphoreGive(g_session_mutex);

    ESP_LOGI(TAG, "Ket thuc phien sac %s | Ly do: %s | Dien tieu thu: %.3f kWh",
             session_snapshot.session_id,
             stop_reason_to_str(reason),
             session_snapshot.energy_used_kwh);

    snprintf(message, msg_size, "Phien %s ket thuc: %.3f kWh",
             session_snapshot.session_id, session_snapshot.energy_used_kwh);

    if (!nvs_save_session(&session_snapshot)) {
        ESP_LOGW(TAG, "Khong the cap nhat session ket thuc vao NVS.");
    }

    return true;
}
// Cập nhật trạng thái trong globals_update_session_energy.
void globals_update_session_energy(const pzem_data_t *sample) {
    if (sample == NULL || !sample->valid) {
        return;
    }

    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!g_current_session.active) {
            reset_energy_integrator_locked(0.0f, 0U);
            xSemaphoreGive(g_session_mutex);
            return;
        }

        const uint32_t sample_ts_ms =
            (sample->timestamp_ms != 0U) ? sample->timestamp_ms : current_timestamp_ms_u32();
        const float sample_power_w = (sample->power > 0.0f) ? sample->power : 0.0f;

        if (s_energy_integrator_last_ts_ms == 0U) {
            reset_energy_integrator_locked(sample_power_w, sample_ts_ms);
            refresh_last_session_snapshot_locked();
            xSemaphoreGive(g_session_mutex);
            return;
        }

        uint32_t elapsed_ms = sample_ts_ms - s_energy_integrator_last_ts_ms;
        s_energy_integrator_last_ts_ms = sample_ts_ms;
        s_energy_integrator_residual_ms += elapsed_ms;

        // Trapezoidal rule: dÃ¹ng trung bÃ¬nh power cÅ© vÃ  má»›i cho chÃ­nh xÃ¡c hÆ¡n
        const float avg_power_w = (s_energy_integrator_power_w + sample_power_w) / 2.0f;
        while (s_energy_integrator_residual_ms >= ENERGY_INTEGRATION_STEP_MS) {
            g_current_session.energy_used_kwh +=
                avg_power_w / 3600000.0f; // W*s -> kWh cho 1 giay
            s_energy_integrator_residual_ms -= ENERGY_INTEGRATION_STEP_MS;
        }

        s_energy_integrator_power_w = sample_power_w;
        if (g_current_session.energy_used_kwh < 0.0f) {
            g_current_session.energy_used_kwh = 0.0f;
        }

        refresh_last_session_snapshot_locked();
        xSemaphoreGive(g_session_mutex);
    }
}
// Cập nhật giá trị trong globals_set_active_session_max_current_limit.
void globals_set_active_session_max_current_limit(float max_current_limit) {
    if (max_current_limit <= 0.0f) {
        return;
    }

    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (g_current_session.active) {
            g_current_session.max_current_limit = max_current_limit;
            refresh_last_session_snapshot_locked();
        }
        xSemaphoreGive(g_session_mutex);
    }
}
// Thực hiện xử lý trong globals_is_session_active.
bool globals_is_session_active(void) {
    bool active = s_last_session_snapshot.active;

    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active = g_current_session.active;
        refresh_last_session_snapshot_locked();
        xSemaphoreGive(g_session_mutex);
    }

    return active;
}
// Lấy dữ liệu trong globals_get_session_snapshot.
void globals_get_session_snapshot(charging_session_t *out_session) {
    if (xSemaphoreTake(g_session_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        memcpy(out_session, &g_current_session, sizeof(charging_session_t));
        refresh_last_session_snapshot_locked();
        xSemaphoreGive(g_session_mutex);
    } else {
        memcpy(out_session, &s_last_session_snapshot, sizeof(charging_session_t));
    }
}


