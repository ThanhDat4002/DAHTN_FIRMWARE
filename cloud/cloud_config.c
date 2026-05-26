
#include "cloud_internal.h"
#include "config.h"
#include "globals.h"
#include "nvs_manager.h"
#include "relay.h"
#include "wifi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "CLOUD_CFG"

static wifi_config_data_t s_firebase_cfg;
static wifi_config_data_t s_cloud_cfg_from_nvs;
static bool     s_has_config        = false;
static bool     s_time_synced       = false;
static bool     s_sntp_initialized  = false;
static uint32_t s_cloud_failure_streak = 0;

// LOAD CONFIG (hardcoded -> NVS override)
// Tải dữ liệu trong load_hardcoded_firebase_config.
static void load_hardcoded_firebase_config(wifi_config_data_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->firebase_url, FIREBASE_DATABASE_URL, sizeof(cfg->firebase_url) - 1);
    strncpy(cfg->firebase_token, FIREBASE_AUTH_TOKEN, sizeof(cfg->firebase_token) - 1);
    strncpy(cfg->device_id, FIREBASE_DEVICE_ID, sizeof(cfg->device_id) - 1);
}
// Áp dụng cấu hình trong apply_nvs_override.
static void apply_nvs_override(wifi_config_data_t *cfg) {
    memset(&s_cloud_cfg_from_nvs, 0, sizeof(s_cloud_cfg_from_nvs));
    if (!nvs_load_cloud_config(&s_cloud_cfg_from_nvs)) {
        ESP_LOGI(TAG, "Dung cloud config mac dinh trong firmware: DevID=%s", cfg->device_id);
        return;
    }

    if (s_cloud_cfg_from_nvs.firebase_url[0] != '\0') {
        strncpy(cfg->firebase_url, s_cloud_cfg_from_nvs.firebase_url, sizeof(cfg->firebase_url) - 1);
        cfg->firebase_url[sizeof(cfg->firebase_url) - 1] = '\0';
    }
    if (s_cloud_cfg_from_nvs.firebase_token[0] != '\0') {
        strncpy(cfg->firebase_token, s_cloud_cfg_from_nvs.firebase_token, sizeof(cfg->firebase_token) - 1);
        cfg->firebase_token[sizeof(cfg->firebase_token) - 1] = '\0';
    }
    if (s_cloud_cfg_from_nvs.device_id[0] != '\0') {
        strncpy(cfg->device_id, s_cloud_cfg_from_nvs.device_id, sizeof(cfg->device_id) - 1);
        cfg->device_id[sizeof(cfg->device_id) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Nap cloud config tu NVS: DevID=%s", cfg->device_id);
}
// Khởi tạo thành phần trong cloud_config_init.
bool cloud_config_init(void) {
    load_hardcoded_firebase_config(&s_firebase_cfg);
    apply_nvs_override(&s_firebase_cfg);

    if (strlen(s_firebase_cfg.firebase_url) == 0 || strlen(s_firebase_cfg.device_id) == 0) {
        ESP_LOGW(TAG, "Thieu Firebase URL hoac Device ID.");
        s_has_config = false;
        return false;
    }

    size_t len = strlen(s_firebase_cfg.firebase_url);
    if (len > 0 && s_firebase_cfg.firebase_url[len - 1] == '/') {
        s_firebase_cfg.firebase_url[len - 1] = '\0';
    }

    s_has_config = true;
    return true;
}
// Thực hiện xử lý trong cloud_config_has_config.
bool cloud_config_has_config(void) {
    return s_has_config;
}

const wifi_config_data_t *cloud_config_get(void) {
    return &s_firebase_cfg;
}

// URL BUILDER
// Tạo nội dung trong cloud_config_build_url.
void cloud_config_build_url(char *out_url, size_t out_len, const char *path) {
    if (out_url == NULL || out_len == 0) {
        return;
    }

    const char *safe_path = (path != NULL) ? path : "";
    if (strlen(s_firebase_cfg.firebase_token) > 0) {
        snprintf(out_url, out_len, "%s/stations/%s%s.json?auth=%s",
                 s_firebase_cfg.firebase_url, s_firebase_cfg.device_id, safe_path,
                 s_firebase_cfg.firebase_token);
    } else {
        snprintf(out_url, out_len, "%s/stations/%s%s.json",
                 s_firebase_cfg.firebase_url, s_firebase_cfg.device_id, safe_path);
    }
}

// NTP
// Thực hiện xử lý trong cloud_config_sync_ntp.
bool cloud_config_sync_ntp(void) {
    if (s_time_synced) {
        time_t now = time(NULL);
        if (now > 946684800) {
            return true;
        }
        s_time_synced = false;
    }

    if (!wifi_is_connected()) {
        return false;
    }

    if (!s_sntp_initialized) {
        const esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t err = esp_netif_sntp_init(&sntp_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Khong khoi tao duoc SNTP: %s", esp_err_to_name(err));
            return false;
        }
        s_sntp_initialized = true;
    } else {
        esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Khong khoi dong lai duoc SNTP: %s", esp_err_to_name(err));
        }
    }

    esp_err_t wait_err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(5000));
    if (wait_err == ESP_OK) {
        time_t now = time(NULL);
        if (now > 946684800) {
            s_time_synced = true;
            ESP_LOGI(TAG, "Dong bo thoi gian NTP thanh cong.");
            return true;
        }
    } else {
        ESP_LOGW(TAG, "Cho NTP sync that bai: %s", esp_err_to_name(wait_err));
    }

    return false;
}

// ONLINE / OFFLINE STATE (cÃ³ debounce)
// Cập nhật giá trị trong cloud_config_set_online.
void cloud_config_set_online(bool online) {
    const bool wifi_up = wifi_is_connected();
    const EventBits_t bits = xEventGroupGetBits(g_network_event_group);
    const bool was_online = (bits & NETWORK_FIREBASE_OK_BIT) != 0;
    charging_session_t session;
    const system_state_t current_state = g_system_state;

    globals_get_session_snapshot(&session);
    const bool charging_active = relay_is_on() || session.active;

    if (!wifi_up) {
        s_cloud_failure_streak = 0;
        xEventGroupClearBits(g_network_event_group, NETWORK_FIREBASE_OK_BIT);
        return;
    }

    if (online) {
        s_cloud_failure_streak = 0;
        xEventGroupSetBits(g_network_event_group, NETWORK_FIREBASE_OK_BIT);
        globals_set_network_status(NET_ONLINE);

        if (!was_online) {
            ESP_LOGI(TAG, "Firebase da truy cap duoc, chuyen he thong ve online.");
        }

        if (charging_active && current_state != STATE_CHARGING && current_state != STATE_FAULT) {
            globals_set_system_state(STATE_CHARGING);
        } else if (!charging_active &&
                   current_state != STATE_FAULT &&
                   current_state != STATE_STOPPED) {
            globals_set_system_state(STATE_READY);
        }
        return;
    }

    // online == false
    if (was_online) {
        s_cloud_failure_streak++;
        if (s_cloud_failure_streak < CLOUD_OFFLINE_FAILURE_THRESHOLD) {
            ESP_LOGW(TAG,
                     "Firebase loi tam thoi (%lu/%lu), giu trang thai online them mot nhip.",
                     (unsigned long)s_cloud_failure_streak,
                     (unsigned long)CLOUD_OFFLINE_FAILURE_THRESHOLD);
            return;
        }
    } else {
        s_cloud_failure_streak = CLOUD_OFFLINE_FAILURE_THRESHOLD;
    }

    xEventGroupClearBits(g_network_event_group, NETWORK_FIREBASE_OK_BIT);
    globals_set_network_status(NET_WIFI_CONNECTED);

    if (was_online) {
        ESP_LOGW(TAG, "Firebase khong con truy cap duoc, chuyen cloud sang che do giam cap.");
    }

    if (charging_active && current_state == STATE_CHARGING) {
        globals_set_system_state(STATE_OFFLINE);
    } else if (!charging_active && current_state == STATE_READY) {
        globals_set_system_state(STATE_IDLE);
    }
}


