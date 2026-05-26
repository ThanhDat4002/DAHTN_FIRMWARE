/**
 * @file wifi.c
 * @brief WiFi STA connection + reconnect backoff + reset button.
 *
 * Captive portal (AP + HTTP form + DNS hijack) Ä‘Ã£ Ä‘Æ°á»£c tÃ¡ch sang `wifi_portal.c`.
 * File nÃ y giá»¯:
 *  - Káº¿t ná»‘i STA tá»« NVS, event handler, network_task (reconnect / backoff)
 *  - Task giÃ¡m sÃ¡t nÃºt WiFi reset / master reset
 *  - Getters Ä‘á»‹a chá»‰ IP / tráº¡ng thÃ¡i káº¿t ná»‘i
 *  - Helper ná»™i bá»™ chia sáº» vá»›i wifi_portal.c qua wifi_internal.h
 */

#include "wifi.h"
#include "wifi_internal.h"
#include "config.h"
#include "globals.h"
#include "nvs_manager.h"
#include "relay.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define TAG "WIFI"
#define WIFI_RECONNECT_JITTER_PCT 20U
#define WIFI_RETRY_LOG_EVERY 5
#define BUTTON_SCAN_PERIOD_MS 20U
#define BUTTON_DEBOUNCE_MS 60U
#define BUTTON_ARM_HIGH_MS 500U
#define BUTTON_ACTION_MIN_UPTIME_S 5U

// INTERNAL STATE
static volatile bool                s_wifi_connected         = false;
static volatile bool                s_config_received        = false;
static volatile bool                s_restart_requested      = false;
static volatile bool                s_sta_config_available   = false;
static volatile bool                s_provisioning_requested = false;
static volatile bool                s_wifi_ever_connected    = false;
static volatile bool                s_sta_connecting         = false;
static volatile uint32_t            s_disconnect_started_ms  = 0;
static volatile wifi_err_reason_t   s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
static wifi_config_data_t           s_boot_wifi_cfg;
static char                         s_ip_str[16]             = "0.0.0.0";

// HELPERS
// Chạy tác vụ trong task_delay_with_wdt.
static void task_delay_with_wdt(uint32_t delay_ms) {
    while (delay_ms > 0) {
        uint32_t slice_ms = (delay_ms > 1000U) ? 1000U : delay_ms;
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        esp_task_wdt_reset();
        delay_ms -= slice_ms;
    }
}
// Thực hiện xử lý trong charging_path_active.
static bool charging_path_active(void) {
    return relay_is_on() ||
           globals_is_session_active() ||
           g_system_state == STATE_CHARGING ||
           g_system_state == STATE_OFFLINE;
}
// Thực hiện xử lý trong safe_to_enter_captive_portal.
static bool safe_to_enter_captive_portal(void) {
    return !charging_path_active();
}
// Thực hiện xử lý trong monotonic_ms.
static uint32_t monotonic_ms(void) {
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}
// Áp dụng cấu hình trong apply_reconnect_jitter.
static uint32_t apply_reconnect_jitter(uint32_t base_ms) {
    if (base_ms == 0U) {
        return 0U;
    }

    const uint32_t jitter_span = (base_ms * WIFI_RECONNECT_JITTER_PCT) / 100U;
    if (jitter_span == 0U) {
        return base_ms;
    }

    const uint32_t random_part = esp_random() % (jitter_span + 1U);
    uint32_t jittered_ms = base_ms + random_part;
    if (jittered_ms > WIFI_RECONNECT_MAX_MS) {
        jittered_ms = WIFI_RECONNECT_MAX_MS;
    }
    return jittered_ms;
}
// Kiểm tra điều kiện trong should_log_retry_attempt.
static bool should_log_retry_attempt(int retry_count) {
    return (retry_count <= 3) || ((retry_count % WIFI_RETRY_LOG_EVERY) == 0);
}
// Thực hiện xử lý trong disconnect_reason_requires_reprovisioning.
static bool disconnect_reason_requires_reprovisioning(wifi_err_reason_t reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_CONNECTION_FAIL:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return true;
        default:
            return false;
    }
}
// Kiểm tra điều kiện trong should_request_provisioning_fallback.
static bool should_request_provisioning_fallback(int retry_count) {
    uint32_t disconnect_duration_ms = 0;

    if (retry_count < WIFI_MAX_RETRY || !safe_to_enter_captive_portal()) {
        return false;
    }

    if (disconnect_reason_requires_reprovisioning(s_last_disconnect_reason)) {
        return true;
    }

    if (s_disconnect_started_ms != 0) {
        disconnect_duration_ms = monotonic_ms() - s_disconnect_started_ms;
        if (disconnect_duration_ms >= WIFI_PROVISIONING_RECOVERY_MS) {
            return true;
        }
    }

    return false;
}
// Khởi tạo thành phần trong init_reset_button.
static void init_reset_button(void) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << WIFI_RESET_BUTTON_PIN) | (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

// CROSS-MODULE HELPERS (used by wifi_portal.c via wifi_internal.h)
// Thực hiện xử lý trong wifi_internal_prepare_for_captive_portal.
void wifi_internal_prepare_for_captive_portal(void) {
    s_config_received        = false;
    s_wifi_connected         = false;
    s_disconnect_started_ms  = 0;
    s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    s_provisioning_requested = false;

    if (g_system_state != STATE_FAULT && !charging_path_active()) {
        if (g_system_state != STATE_STOPPED) {
            globals_set_system_state(STATE_IDLE);
        }
    }
    globals_set_network_status(NET_CAPTIVE_PORTAL);
    xEventGroupClearBits(g_network_event_group,
                         NETWORK_WIFI_CONNECTED_BIT | NETWORK_FIREBASE_OK_BIT);
}
// Thực hiện xử lý trong wifi_internal_on_portal_config_saved.
void wifi_internal_on_portal_config_saved(void) {
    s_config_received       = true;
    s_restart_requested     = true;
    s_sta_config_available  = true;
}

// WIFI EVENT HANDLER
// Xử lý sự kiện trong wifi_event_handler.
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data) {
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                esp_err_t connect_err = esp_wifi_connect();
                if (connect_err == ESP_OK) {
                    s_sta_connecting = true;
                } else {
                    s_sta_connecting = false;
                    ESP_LOGW(TAG, "esp_wifi_connect that bai o STA_START: %s",
                             esp_err_to_name(connect_err));
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                const wifi_event_sta_disconnected_t *event =
                    (const wifi_event_sta_disconnected_t *)event_data;

                s_wifi_connected = false;
                s_sta_connecting = false;
                if (s_disconnect_started_ms == 0) {
                    s_disconnect_started_ms = monotonic_ms();
                }
                if (event != NULL) {
                    s_last_disconnect_reason = event->reason;
                }
                strncpy(s_ip_str, "0.0.0.0", sizeof(s_ip_str) - 1);
                s_ip_str[sizeof(s_ip_str) - 1] = '\0';
                globals_set_network_status(
                    wifi_portal_is_active() ? NET_CAPTIVE_PORTAL : NET_DISCONNECTED);
                xEventGroupClearBits(g_network_event_group,
                                     NETWORK_WIFI_CONNECTED_BIT | NETWORK_FIREBASE_OK_BIT);
                ESP_LOGW(TAG, "WiFi bi ngat, ly_do=%d.", (int)s_last_disconnect_reason);
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Thiet bi da ket noi toi AP cai dat.");
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected         = true;
        s_sta_connecting         = false;
        s_wifi_ever_connected    = true;
        s_disconnect_started_ms  = 0;
        s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
        s_provisioning_requested = false;
        globals_set_network_status(NET_WIFI_CONNECTED);
        xEventGroupSetBits(g_network_event_group, NETWORK_WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "WiFi da ket noi, IP: %s", s_ip_str);
    }
}

// WIFI RESET BUTTON TASK
// Đặt lại trạng thái trong wifi_reset_button_task.
void wifi_reset_button_task(void *pvParameters) {
    (void)pvParameters;

    ESP_LOGI(TAG, "Tac vu nut reset WiFi bat dau o Core %d", xPortGetCoreID());
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    bool pressed = false;
    bool armed = false;
    uint32_t pressed_ms = 0;
    bool combo_pressed = false;
    uint32_t combo_pressed_ms = 0;
    uint32_t wifi_high_stable_ms = 0;
    bool wifi_raw_last = false;
    bool boot_raw_last = false;
    bool wifi_stable_low = false;
    bool boot_stable_low = false;
    uint32_t wifi_change_ms = monotonic_ms();
    uint32_t boot_change_ms = wifi_change_ms;

    while (1) {
        const uint32_t now_ms = monotonic_ms();
        const bool wifi_is_low_raw = (gpio_get_level(WIFI_RESET_BUTTON_PIN) == 0);
        const bool boot_is_low_raw = (gpio_get_level(BOOT_BUTTON_PIN) == 0);

        if (wifi_is_low_raw != wifi_raw_last) {
            wifi_raw_last = wifi_is_low_raw;
            wifi_change_ms = now_ms;
        } else if ((now_ms - wifi_change_ms) >= BUTTON_DEBOUNCE_MS) {
            wifi_stable_low = wifi_is_low_raw;
        }

        if (boot_is_low_raw != boot_raw_last) {
            boot_raw_last = boot_is_low_raw;
            boot_change_ms = now_ms;
        } else if ((now_ms - boot_change_ms) >= BUTTON_DEBOUNCE_MS) {
            boot_stable_low = boot_is_low_raw;
        }

        const bool wifi_is_low = wifi_stable_low;
        const bool boot_is_low = boot_stable_low;
        const bool combo_is_low = wifi_is_low && boot_is_low;

        if (!wifi_is_low) {
            if (wifi_high_stable_ms < BUTTON_ARM_HIGH_MS) {
                wifi_high_stable_ms += BUTTON_SCAN_PERIOD_MS;
                if (wifi_high_stable_ms > BUTTON_ARM_HIGH_MS) {
                    wifi_high_stable_ms = BUTTON_ARM_HIGH_MS;
                }
            }
        } else {
            wifi_high_stable_ms = 0;
        }

        if (combo_is_low) {
            if (!combo_pressed) {
                combo_pressed = true;
                combo_pressed_ms = 0;
            } else {
                combo_pressed_ms += BUTTON_SCAN_PERIOD_MS;
            }

            pressed = false;
            armed = false;
            pressed_ms = 0;

            if (combo_pressed_ms >= MASTER_RESET_HOLD_MS &&
                g_uptime_seconds >= BUTTON_ACTION_MIN_UPTIME_S) {
                relay_off();
                nvs_clear_session();
                nvs_clear_fault_latch();
                globals_set_error_code(ERR_NONE);
                globals_set_system_state(
                    (globals_get_network_status() == NET_ONLINE) ? STATE_READY : STATE_IDLE);
                beep(3, 150);
                ESP_LOGW(TAG, "Master Reset: da xoa session NVS + clear FAULT, dua he thong ve trang thai cho.");
                combo_pressed = false;
                combo_pressed_ms = 0;
                while (gpio_get_level(WIFI_RESET_BUTTON_PIN) == 0 ||
                       gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                    task_delay_with_wdt(BUTTON_SCAN_PERIOD_MS);
                }
            }

            task_delay_with_wdt(BUTTON_SCAN_PERIOD_MS);
            continue;
        } else {
            combo_pressed = false;
            combo_pressed_ms = 0;
        }

        if (!wifi_is_low) {
            armed = (wifi_high_stable_ms >= BUTTON_ARM_HIGH_MS);
            pressed = false;
            pressed_ms = 0;
        } else if (armed) {
            if (!pressed) {
                pressed = true;
                pressed_ms = 0;
            } else {
                pressed_ms += BUTTON_SCAN_PERIOD_MS;
                if (pressed_ms >= WIFI_RESET_BUTTON_HOLD_MS &&
                    g_uptime_seconds >= BUTTON_ACTION_MIN_UPTIME_S) {
                    if (charging_path_active()) {
                        ESP_LOGW(TAG, "Bo qua nut reset WiFi khi dang sac hoac dang khoi phuc session.");
                        pressed = false;
                        armed = false;
                        pressed_ms = 0;
                    } else {
                        ESP_LOGW(TAG, "Nut reset WiFi duoc giu, dang xoa cau hinh va khoi dong lai...");
                        nvs_clear_wifi_config();
                        task_delay_with_wdt(500);
                        esp_restart();
                    }
                }
            }
        }

        task_delay_with_wdt(BUTTON_SCAN_PERIOD_MS);
    }
}

// INIT
// Khởi tạo thành phần trong wifi_init.
bool wifi_init(void) {
    wifi_config_t sta_cfg = {0};

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    init_reset_button();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_provisioning_requested = false;
    memset(&s_boot_wifi_cfg, 0, sizeof(s_boot_wifi_cfg));
    s_sta_config_available = nvs_load_wifi_config(&s_boot_wifi_cfg) &&
                             (strlen(s_boot_wifi_cfg.ssid) > 0);

    if (!s_sta_config_available) {
        if (safe_to_enter_captive_portal()) {
            ESP_LOGW(TAG, "Khong tim thay cau hinh WiFi, dang bat che do Captive Portal.");
            wifi_portal_start();
        } else {
            ESP_LOGW(TAG, "Khong tim thay cau hinh WiFi, nhung thiet bi dang sac/khoi phuc; tam thoi giu offline.");
            globals_set_network_status(NET_DISCONNECTED);
        }
        return false;
    }

    const bool has_wifi_password = (s_boot_wifi_cfg.password[0] != '\0');

    ESP_LOGI(TAG, "Dang thu ket noi WiFi SSID: %s (%s)",
             s_boot_wifi_cfg.ssid,
             has_wifi_password ? "co mat khau" : "khong mat khau");
    globals_set_network_status(NET_CONNECTING);
    s_disconnect_started_ms = monotonic_ms();
    s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;

    strncpy((char *)sta_cfg.sta.ssid, s_boot_wifi_cfg.ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, s_boot_wifi_cfg.password, sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = has_wifi_password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    return true;
}

// NETWORK TASK (reconnect / backoff)
// Chạy tác vụ trong network_task.
void network_task(void *pvParameters) {
    (void)pvParameters;

    uint32_t backoff_ms = WIFI_RECONNECT_BASE_MS;
    int retry_count = 0;

    ESP_LOGI(TAG, "Tac vu mang bat dau o Core %d", xPortGetCoreID());
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (1) {
        task_delay_with_wdt(WIFI_MANAGER_TICK_MS);

        if (s_restart_requested && s_config_received) {
            task_delay_with_wdt(2000);
            ESP_LOGI(TAG, "Da luu cau hinh WiFi, dang khoi dong lai thiet bi.");
            esp_restart();
        }

        if (!s_sta_config_available) {
            if (!wifi_portal_is_active() && safe_to_enter_captive_portal()) {
                wifi_portal_start();
            }
            continue;
        }

        if (s_provisioning_requested) {
            if (!wifi_portal_is_active() && safe_to_enter_captive_portal()) {
                ESP_LOGW(TAG, "Chuyen sang che do Captive Portal sau khi vuot qua gioi han thu lai WiFi.");
                wifi_portal_start();
            } else if (!wifi_portal_is_active()) {
                globals_set_network_status(NET_DISCONNECTED);
            }
            continue;
        }

        if (wifi_portal_is_active()) {
            continue;
        }

        if (s_wifi_connected) {
            backoff_ms = WIFI_RECONNECT_BASE_MS;
            retry_count = 0;
            continue;
        }

        if (s_sta_connecting) {
            continue;
        }

        if (s_disconnect_started_ms == 0) {
            s_disconnect_started_ms = monotonic_ms();
        }

        if (g_system_state == STATE_CHARGING) {
            globals_set_system_state(STATE_OFFLINE);
            globals_set_network_status(NET_DISCONNECTED);
            ESP_LOGW(TAG, "Mat WiFi trong luc dang sac, chuyen sang OFFLINE.");
        } else if (g_system_state == STATE_READY) {
            globals_set_system_state(STATE_IDLE);
            globals_set_network_status(NET_DISCONNECTED);
        }

        retry_count++;
        if (should_request_provisioning_fallback(retry_count)) {
            const uint32_t disconnect_duration_ms = monotonic_ms() - s_disconnect_started_ms;
            s_provisioning_requested = true;
            globals_set_network_status(NET_DISCONNECTED);
            ESP_LOGW(TAG,
                     "Da toi gioi han thu lai WiFi; yeu cau quay ve provisioning (ly_do=%d, mat_ket_noi=%lu ms, da_tung_ket_noi=%d).",
                     (int)s_last_disconnect_reason,
                     (unsigned long)disconnect_duration_ms,
                     (int)s_wifi_ever_connected);
            continue;
        }

        const uint32_t backoff_with_jitter_ms = apply_reconnect_jitter(backoff_ms);
        globals_set_network_status(NET_CONNECTING);
        if (should_log_retry_attempt(retry_count)) {
            ESP_LOGW(TAG, "Thu lai ket noi WiFi lan #%d sau %lu ms", retry_count,
                     (unsigned long)backoff_with_jitter_ms);
        }

        esp_err_t connect_err = esp_wifi_connect();
        if (connect_err == ESP_OK) {
            s_sta_connecting = true;
        } else {
            s_sta_connecting = false;
            ESP_LOGW(TAG, "esp_wifi_connect that bai o tac_vu_mang: %s", esp_err_to_name(connect_err));
        }
        task_delay_with_wdt(backoff_with_jitter_ms);

        backoff_ms = (backoff_ms * 2 > WIFI_RECONNECT_MAX_MS)
                         ? WIFI_RECONNECT_MAX_MS
                         : backoff_ms * 2;
    }
}

// GETTERS
// Lấy dữ liệu trong wifi_get_ip.
void wifi_get_ip(char *buf, size_t buflen) {
    if (buflen == 0) {
        return;
    }
    strncpy(buf, s_ip_str, buflen - 1);
    buf[buflen - 1] = '\0';
}
// Thực hiện xử lý trong wifi_is_connected.
bool wifi_is_connected(void) {
    return s_wifi_connected;
}


