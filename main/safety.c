/**
 * @file safety.c
 * @brief TÃ¡c vá»¥ giÃ¡m sÃ¡t an toÃ n: dÃ²ng / Ã¡p / unplug / trickle / target / relay-stuck.
 *
 * `safety_task` Ä‘Ã£ Ä‘Æ°á»£c refactor thÃ nh nhiá»u helper static Ä‘á»ƒ má»—i check
 * cÃ³ thá»ƒ Ä‘á»c Ä‘á»™c láº­p. Má»—i check tráº£ vá» TRUE náº¿u Ä‘Ã£ xá»­ lÃ½ xong vÃ²ng láº·p
 * (caller pháº£i `continue`); FALSE náº¿u chÆ°a cÃ³ action vÃ  loop tiáº¿p tá»¥c.
 */

#include "safety.h"
#include "globals.h"
#include "config.h"
#include "relay.h"
#include "ui.h"
#include "nvs_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#define TAG "SAFETY"
#define TARGET_ENERGY_EPSILON_KWH 0.0005f
#define RELAY_FAULT_SETTLE_MS 2000UL
#define RELAY_FAULT_MIN_CURRENT_A 0.5f
#define RELAY_FAULT_CONFIRM_SAMPLES 3
#define SAFETY_IDLE_INVALID_LOG_PERIOD_MS 15000UL

// SAFETY STATE (private)
static bool     s_is_trickle          = false;
static uint64_t s_trickle_start_ms    = 0;

static bool     s_is_unplug_counting  = false;
static uint64_t s_unplug_start_ms     = 0;
static bool     s_is_no_load_counting = false;
static uint64_t s_no_load_start_ms    = 0;

static uint64_t s_last_nvs_save_ms    = 0;
static bool     s_prev_relay_on       = false;
static uint64_t s_relay_on_since_ms   = 0;
static uint64_t s_relay_off_since_ms  = 0;
static bool     s_sensor_seen_valid_after_relay_on = false;
static bool     s_unplug_guard_armed  = false;
static int      s_unplug_arm_streak   = 0;
static int      s_sensor_invalid_after_grace_count = 0;
static int      s_relay_fault_streak   = 0;
static int      s_over_voltage_streak  = 0;
static int      s_under_voltage_streak = 0;
static int      s_over_current_streak  = 0;
static char     s_last_saved_session_id[MAX_SESSION_ID_LEN] = { 0 };
static uint64_t s_last_invalid_idle_log_ms = 0;
static uint64_t s_last_warmup_log_ms = 0;

// LOCAL HELPERS
// Thực hiện xử lý trong now_ms.
static uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}
// Cập nhật trạng thái trong safety_update_relay_edge_state.
static void safety_update_relay_edge_state(void) {
    const bool relay_on_now = relay_is_on();
    if (relay_on_now && !s_prev_relay_on) {
        s_relay_on_since_ms = now_ms();
        s_relay_off_since_ms = 0;
        s_sensor_seen_valid_after_relay_on = false;
        s_unplug_guard_armed = false;
        s_unplug_arm_streak = 0;
        s_is_no_load_counting = false;
        s_no_load_start_ms = 0;
        s_sensor_invalid_after_grace_count = 0;
        s_relay_fault_streak = 0;
        s_over_voltage_streak = 0;
        s_under_voltage_streak = 0;
        s_over_current_streak = 0;
    } else if (!relay_on_now && s_prev_relay_on) {
        s_relay_off_since_ms = now_ms();
        s_relay_on_since_ms = 0;
        s_sensor_seen_valid_after_relay_on = false;
        s_unplug_guard_armed = false;
        s_unplug_arm_streak = 0;
        s_is_no_load_counting = false;
        s_no_load_start_ms = 0;
        s_sensor_invalid_after_grace_count = 0;
        s_relay_fault_streak = 0;
        s_over_voltage_streak = 0;
        s_under_voltage_streak = 0;
        s_over_current_streak = 0;
    }
    s_prev_relay_on = relay_on_now;
}
// Thực hiện xử lý trong fault_stop.
static void fault_stop(error_code_t err, stop_reason_t stop_reason, const char *reason) {
    char msg[128];

    relay_off();
    globals_set_error_code(err);
    globals_set_system_state(STATE_FAULT);
    if (!nvs_save_fault_latch(err)) {
        ESP_LOGW(TAG, "Khong the luu FAULT latch vao NVS.");
    }

    s_is_trickle         = false;
    s_is_unplug_counting = false;
    s_is_no_load_counting = false;
    s_unplug_guard_armed = false;
    s_unplug_arm_streak  = 0;
    s_no_load_start_ms = 0;
    s_relay_fault_streak = 0;
    s_over_voltage_streak = 0;
    s_under_voltage_streak = 0;
    s_over_current_streak = 0;

    if (!globals_finish_session(stop_reason, msg, sizeof(msg))) {
        ESP_LOGW(TAG, "Khong co session active de chot khi vao FAULT.");
    }

    ESP_LOGE(TAG, "NGAT SU CO: %s (loi: %s)", reason, error_code_to_str(err));
}
// Thực hiện xử lý trong normal_stop.
static void normal_stop(error_code_t err,
                        stop_reason_t stop_reason,
                        const char *reason,
                        int beep_count) {
    char msg[128];

    relay_off();
    globals_set_error_code(err);
    globals_set_system_state(STATE_STOPPED);

    s_is_trickle         = false;
    s_is_unplug_counting = false;
    s_is_no_load_counting = false;
    s_unplug_guard_armed = false;
    s_unplug_arm_streak  = 0;
    s_no_load_start_ms = 0;
    s_relay_fault_streak = 0;
    s_over_voltage_streak = 0;
    s_under_voltage_streak = 0;
    s_over_current_streak = 0;

    ESP_LOGI(TAG, "Dung sac: %s", reason);
    if (!globals_finish_session(stop_reason, msg, sizeof(msg))) {
        ESP_LOGW(TAG, "Khong co session active de chot khi vao STOPPED.");
    }

    beep(beep_count, 200);
}

// CHECK HELPERS â€” má»—i hÃ m tráº£ vá» TRUE náº¿u vÃ²ng láº·p nÃªn `continue`
/** Xá»­ lÃ½ máº«u sensor invalid: grace period, Ä‘áº¿m lá»—i, fault sau quÃ¡ ngÆ°á»¡ng. */
// Xử lý sự kiện trong safety_handle_invalid_sensor.
static bool safety_handle_invalid_sensor(bool relay_on_now) {
    const uint64_t now = now_ms();

    if (g_system_state != STATE_CHARGING && g_system_state != STATE_OFFLINE) {
        s_sensor_invalid_after_grace_count = 0;
        if (globals_get_error_code() == ERR_SENSOR_ERROR) {
            globals_set_error_code(ERR_NONE);
        }
        if ((now - s_last_invalid_idle_log_ms) >= SAFETY_IDLE_INVALID_LOG_PERIOD_MS) {
            ESP_LOGW(TAG, "Bo qua mau cam bien khong hop le khi khong sac.");
            s_last_invalid_idle_log_ms = now;
        }
        return true;
    }

    if (relay_on_now &&
        !s_sensor_seen_valid_after_relay_on &&
        s_relay_on_since_ms > 0 &&
        (now - s_relay_on_since_ms) < SENSOR_BOOT_GRACE_MS) {
        if ((now - s_last_warmup_log_ms) >= 1000U) {
            ESP_LOGW(TAG, "Cho cam bien khoi dong trong %lums dau sau khi bat relay.",
                     (unsigned long)SENSOR_BOOT_GRACE_MS);
            s_last_warmup_log_ms = now;
        }
        return true;
    }

    if (s_sensor_invalid_after_grace_count < SENSOR_POST_GRACE_MAX_ERRORS) {
        s_sensor_invalid_after_grace_count++;
    }
    if (s_sensor_invalid_after_grace_count == 1) {
        ESP_LOGW(TAG, "Sensor loi sau grace, bat dau dem xac nhan (%d mau).",
                 SENSOR_POST_GRACE_MAX_ERRORS);
    }
    if (s_sensor_invalid_after_grace_count < SENSOR_POST_GRACE_MAX_ERRORS) {
        return true;
    }

    fault_stop(ERR_SENSOR_ERROR, STOP_REASON_STATION_FAULT,
               "Mat ket noi cam bien PZEM-004T");
    beep(3, 300);
    s_sensor_invalid_after_grace_count = 0;
    return true;
}

/** Cáº­p nháº­t tráº¡ng thÃ¡i "Ä‘Ã£ arm unplug" khi Ä‘ang báº­t relay. */
// Cập nhật trạng thái trong safety_update_unplug_arm.
static void safety_update_unplug_arm(const pzem_data_t *data, bool relay_on_now) {
    if (!relay_on_now) {
        return;
    }

    s_sensor_seen_valid_after_relay_on = true;
    if (s_unplug_guard_armed) {
        return;
    }

    const bool arm_candidate =
        (data->current >= UNPLUG_ARM_CURRENT_A) &&
        (data->power >= UNPLUG_ARM_MIN_POWER_W);

    if (!arm_candidate) {
        s_unplug_arm_streak = 0;
        return;
    }

    if (s_unplug_arm_streak < UNPLUG_ARM_CONFIRM_SAMPLES) {
        s_unplug_arm_streak++;
    }
    if (s_unplug_arm_streak >= UNPLUG_ARM_CONFIRM_SAMPLES) {
        s_unplug_guard_armed = true;
        ESP_LOGI(TAG, "Da arm chong trom dien (%d mau): I=%.3fA P=%.1fW.",
                 UNPLUG_ARM_CONFIRM_SAMPLES, data->current, data->power);
    }
}

/** PhÃ¡t hiá»‡n relay Ä‘Ã£ Táº®T mÃ  váº«n cÃ³ dÃ²ng (hÆ° cá»©ng tiáº¿p Ä‘iá»ƒm). */
// Thực hiện xử lý trong safety_check_relay_stuck_on.
static bool safety_check_relay_stuck_on(const pzem_data_t *data, bool relay_on_now) {
    if (relay_on_now || data->current <= RELAY_FAULT_MIN_CURRENT_A) {
        s_relay_fault_streak = 0;
        return false;
    }

    const uint32_t off_ts_ms_32 = (uint32_t)s_relay_off_since_ms;
    const uint32_t sample_ts_ms_32 =
        (data->timestamp_ms != 0U) ? data->timestamp_ms : off_ts_ms_32;
    const bool off_stable =
        (s_relay_off_since_ms > 0) &&
        ((now_ms() - s_relay_off_since_ms) >= RELAY_FAULT_SETTLE_MS);
    const bool sample_after_off =
        (s_relay_off_since_ms > 0) &&
        ((uint32_t)(sample_ts_ms_32 - off_ts_ms_32) < 0x80000000U);

    if (!off_stable || !sample_after_off) {
        s_relay_fault_streak = 0;
        return false;
    }

    if (s_relay_fault_streak < RELAY_FAULT_CONFIRM_SAMPLES) {
        s_relay_fault_streak++;
    }

    if (s_relay_fault_streak >= RELAY_FAULT_CONFIRM_SAMPLES) {
        fault_stop(ERR_RELAY_FAULT, STOP_REASON_STATION_FAULT,
                   "LOI RELAY: Relay da TAT nhung van co dong!");
        ESP_LOGE(TAG, "LOI RELAY: Relay da TAT nhung van co dong %.3fA!", data->current);
        beep(5, 300);
        s_relay_fault_streak = 0;
        return true;
    }

    if (s_relay_fault_streak == 1) {
        ESP_LOGW(TAG, "Nghi ngo relay fault, bat dau dem xac nhan (%d mau): I=%.3fA khi relay OFF.",
                 RELAY_FAULT_CONFIRM_SAMPLES, data->current);
    }
    return false;
}

/** Reset toÃ n bá»™ counter khi khÃ´ng trong tráº¡ng thÃ¡i CHARGING/OFFLINE. */
// Đặt lại trạng thái trong safety_reset_on_idle_state.
static void safety_reset_on_idle_state(void) {
    s_is_trickle          = false;
    s_is_unplug_counting  = false;
    s_is_no_load_counting = false;
    s_unplug_guard_armed  = false;
    s_unplug_arm_streak   = 0;
    s_no_load_start_ms    = 0;
    s_over_voltage_streak = 0;
    s_under_voltage_streak = 0;
    s_over_current_streak = 0;

    if (globals_get_error_code() == ERR_SENSOR_ERROR) {
        globals_set_error_code(ERR_NONE);
    }
}

/** QuÃ¡ dÃ²ng -> FAULT sau N máº«u liÃªn tiáº¿p (Ä‘á»“ng bá»™ kiá»ƒu lá»c vá»›i quÃ¡/tháº¥p Ã¡p). */
// Thực hiện xử lý trong safety_check_overcurrent.
static bool safety_check_overcurrent(const pzem_data_t *data, float limit) {
    if (data->current <= limit) {
        s_over_current_streak = 0;
        return false;
    }

    if (s_over_current_streak < OVER_CURRENT_CONFIRM_SAMPLES) {
        s_over_current_streak++;
    }

    if (s_over_current_streak >= OVER_CURRENT_CONFIRM_SAMPLES) {
        fault_stop(ERR_OVER_CURRENT, STOP_REASON_OVERLOAD, "Dong dien vuot nguong!");
        beep(3, 500);
        s_over_current_streak = 0;
        return true;
    }

    if (s_over_current_streak == 1) {
        ESP_LOGW(TAG, "Nghi ngo qua dong, bat dau dem xac nhan (%d mau): I=%.3fA limit=%.3fA",
                 OVER_CURRENT_CONFIRM_SAMPLES, data->current, limit);
    }
    return false;
}

/** QuÃ¡/dÆ°á»›i Ã¡p -> FAULT sau N máº«u xÃ¡c nháº­n. */
// Thực hiện xử lý trong safety_check_voltage.
static bool safety_check_voltage(const pzem_data_t *data) {
    if (data->voltage > MAX_VOLTAGE_V && data->voltage > 0.1f) {
        if (s_over_voltage_streak < OVER_VOLTAGE_CONFIRM_SAMPLES) {
            s_over_voltage_streak++;
        }
        s_under_voltage_streak = 0;
        if (s_over_voltage_streak >= OVER_VOLTAGE_CONFIRM_SAMPLES) {
            fault_stop(ERR_OVER_VOLTAGE, STOP_REASON_STATION_FAULT,
                       "Dien ap luoi qua cao (vuot nguong MAX_VOLTAGE_V)");
            beep(2, 300);
            return true;
        }
        if (s_over_voltage_streak == 1) {
            ESP_LOGW(TAG, "Nghi ngo qua ap, bat dau dem xac nhan (%d mau): %.2fV",
                     OVER_VOLTAGE_CONFIRM_SAMPLES, data->voltage);
        }
        return false;
    }

    if (data->voltage < MIN_VOLTAGE_V && data->voltage > 1.0f) {
        if (s_under_voltage_streak < UNDER_VOLTAGE_CONFIRM_SAMPLES) {
            s_under_voltage_streak++;
        }
        s_over_voltage_streak = 0;
        if (s_under_voltage_streak >= UNDER_VOLTAGE_CONFIRM_SAMPLES) {
            fault_stop(ERR_UNDER_VOLTAGE, STOP_REASON_STATION_FAULT,
                       "Dien ap luoi qua thap (duoi nguong MIN_VOLTAGE_V)");
            beep(2, 300);
            return true;
        }
        if (s_under_voltage_streak == 1) {
            ESP_LOGW(TAG, "Nghi ngo thap ap, bat dau dem xac nhan (%d mau): %.2fV",
                     UNDER_VOLTAGE_CONFIRM_SAMPLES, data->voltage);
        }
        return false;
    }

    s_over_voltage_streak = 0;
    s_under_voltage_streak = 0;
    return false;
}

/** ÄÃ£ Ä‘áº¡t target nÄƒng lÆ°á»£ng -> STOPPED thÃ nh cÃ´ng. */
// Thực hiện xử lý trong safety_check_target_energy.
static bool safety_check_target_energy(const charging_session_t *session) {
    if (!session->active || session->target_energy_kwh <= 0.0f) {
        return false;
    }
    const float target_kwh = session->target_energy_kwh;
    const float used_kwh = session->energy_used_kwh;
    if ((used_kwh + TARGET_ENERGY_EPSILON_KWH) < target_kwh) {
        return false;
    }

    normal_stop(ERR_NONE, STOP_REASON_COMPLETED,
                "Da dat muc tieu nang luong tu server", 2);
    ESP_LOGI(TAG, "Dat target: %.4f/%.4f kWh -> da ngat relay.",
             used_kwh, target_kwh);
    return true;
}

/** Detect unplug (sau khi Ä‘Ã£ arm) hoáº·c no-load (chÆ°a arm). */
// Thực hiện xử lý trong safety_check_unplug_or_noload.
static bool safety_check_unplug_or_noload(const pzem_data_t *data, bool relay_on_now) {
    if (!s_unplug_guard_armed) {
        const bool past_relay_grace =
            relay_on_now &&
            s_relay_on_since_ms > 0 &&
            ((now_ms() - s_relay_on_since_ms) >= SENSOR_BOOT_GRACE_MS);

        if (past_relay_grace && data->current <= ZERO_CURRENT_EPS_A) {
            if (!s_is_no_load_counting) {
                s_is_no_load_counting = true;
                s_no_load_start_ms = now_ms();
                ESP_LOGW(TAG, "Khong phat hien dong tai sau bat relay - bat dau dem %lums...",
                         (unsigned long)UNPLUG_DETECT_MS);
            } else if ((now_ms() - s_no_load_start_ms) >= UNPLUG_DETECT_MS) {
                normal_stop(ERR_UNPLUGGED, STOP_REASON_UNPLUGGED,
                            "Khong co tai sau khi bat relay", 3);
                return true;
            }
        } else {
            s_is_no_load_counting = false;
        }

        s_is_unplug_counting = false;
        return false;
    }

    // Guard armed:
    if (data->current <= ZERO_CURRENT_EPS_A) {
        s_is_no_load_counting = false;
        if (!s_is_unplug_counting) {
            s_is_unplug_counting = true;
            s_unplug_start_ms    = now_ms();
            ESP_LOGW(TAG, "Dong = 0A - bat dau dem %lums chong trom dien...",
                     (unsigned long)UNPLUG_DETECT_MS);
        } else if (now_ms() - s_unplug_start_ms >= UNPLUG_DETECT_MS) {
            normal_stop(ERR_UNPLUGGED, STOP_REASON_UNPLUGGED,
                        "Sung sac bi rut!", 3);
            return true;
        }
        s_is_trickle = false;
        return true; // skip trickle/error checks while counting unplug
    }

    s_is_no_load_counting = false;
    s_is_unplug_counting = false;
    return false;
}

/** Trickle 5 phÃºt -> FULL_CHARGE. */
// Thực hiện xử lý trong safety_check_trickle_full_charge.
static bool safety_check_trickle_full_charge(const pzem_data_t *data) {
    if (data->current > ZERO_CURRENT_EPS_A && data->current <= TRICKLE_CURRENT_A) {
        if (!s_is_trickle) {
            s_is_trickle = true;
            s_trickle_start_ms = now_ms();
            ESP_LOGW(TAG, "Phat hien dong trickle (%.3fA) - bat dau dem 5 phut...",
                     data->current);
            return false;
        }

        uint64_t elapsed = now_ms() - s_trickle_start_ms;
        ESP_LOGD(TAG, "Trickle: %.3fA, da dem: %llu/%lu ms",
                 data->current, elapsed, (unsigned long)AUTO_CUTOFF_MS);

        if (elapsed >= AUTO_CUTOFF_MS) {
            normal_stop(ERR_FULL_BATTERY, STOP_REASON_FULL_CHARGE,
                        "Pin da sac day (5 phut dong trickle)", 2);
            ESP_LOGI(TAG, "SAC DAY: tu dong tat relay va chot phien.");
            return true;
        }
        return false;
    }

    if (data->current > TRICKLE_CURRENT_A) {
        if (s_is_trickle) {
            ESP_LOGD(TAG, "Dong phuc hoi (%.3fA) - huy dem trickle.", data->current);
            s_is_trickle = false;
        }
    }
    return false;
}

/** XÃ³a error code táº¡m thá»i (giá»¯ FULL_BATTERY). */
// Xóa dữ liệu trong safety_clear_transient_errors.
static void safety_clear_transient_errors(void) {
    error_code_t current_error = globals_get_error_code();
    if (current_error != ERR_NONE && current_error != ERR_FULL_BATTERY) {
        globals_set_error_code(ERR_NONE);
    }
}

/** LÆ°u session vÃ o NVS Ä‘á»‹nh ká»³ trong khi sáº¡c. */
// Xử lý sự kiện trong safety_handle_nvs_periodic_save.
static void safety_handle_nvs_periodic_save(const charging_session_t *session) {
    uint64_t curr_ms = now_ms();
    if (!session->active) {
        s_last_saved_session_id[0] = '\0';
        return;
    }

    if (strcmp(s_last_saved_session_id, session->session_id) != 0) {
        strncpy(s_last_saved_session_id,
                session->session_id,
                sizeof(s_last_saved_session_id) - 1);
        s_last_saved_session_id[sizeof(s_last_saved_session_id) - 1] = '\0';
        s_last_nvs_save_ms = curr_ms;
    }

    if ((curr_ms - s_last_nvs_save_ms) >= NVS_SESSION_SAVE_PERIOD_MS) {
        s_last_nvs_save_ms = curr_ms;
        nvs_save_session(session);
        ESP_LOGD(TAG, "Tu dong luu session vao NVS (%.3f kWh)", session->energy_used_kwh);
    }
}

// TASK MAIN LOOP
// Chạy tác vụ trong safety_task.
void safety_task(void *pvParameters) {
    (void)pvParameters;

    ESP_LOGI(TAG, "Tac vu an toan bat dau (Core %d)", xPortGetCoreID());
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    pzem_data_t data;

    while (1) {
        safety_update_relay_edge_state();

        if (xQueueReceive(g_sensor_queue, &data, pdMS_TO_TICKS(500)) != pdTRUE) {
            esp_task_wdt_reset();
            continue;
        }

        esp_task_wdt_reset();
        const bool relay_on_now = relay_is_on();
        // Nha CPU 1 tick de IDLE1 duoc chay, tranh task_wdt khi tai Core 1 cao.
        vTaskDelay(pdMS_TO_TICKS(1));

        if (!data.valid) {
            safety_handle_invalid_sensor(relay_on_now);
            continue;
        }

        safety_update_unplug_arm(&data, relay_on_now);
        s_sensor_invalid_after_grace_count = 0;

        if (safety_check_relay_stuck_on(&data, relay_on_now)) {
            continue;
        }

        if (g_system_state != STATE_CHARGING && g_system_state != STATE_OFFLINE) {
            safety_reset_on_idle_state();
            continue;
        }

        charging_session_t session_snapshot;
        globals_get_session_snapshot(&session_snapshot);
        const float effective_current_limit =
            (session_snapshot.active && session_snapshot.max_current_limit > 0.0f)
                ? session_snapshot.max_current_limit
                : globals_get_max_current_limit();

        if (safety_check_overcurrent(&data, effective_current_limit)) {
            continue;
        }

        if (safety_check_voltage(&data)) {
            continue;
        }

        if (safety_check_target_energy(&session_snapshot)) {
            continue;
        }

        if (safety_check_unplug_or_noload(&data, relay_on_now)) {
            continue;
        }

        if (safety_check_trickle_full_charge(&data)) {
            continue;
        }

        safety_clear_transient_errors();
        safety_handle_nvs_periodic_save(&session_snapshot);
    }
}


