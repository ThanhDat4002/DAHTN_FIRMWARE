/**
 * @file cloud.c
 * @brief Module Cloud: khá»Ÿi táº¡o, publish telemetry, poll lá»‡nh tá»« Firebase.
 *
 * Logic chi tiáº¿t Ä‘Æ°á»£c chia thÃ nh cÃ¡c module ná»™i bá»™:
 *  - cloud_http    : transport HTTP (mutex serial-hÃ³a)
 *  - cloud_config  : Firebase config, URL builder, NTP, online/offline state
 *  - cloud_command : parse + dispatch lá»‡nh tá»« Firebase, Ã¡p dá»¥ng remote config
 *  - cloud_report  : gá»­i session report lÃªn backend 
 *
 * File nÃ y chá»‰ giá»¯: hÃ m tiá»‡n Ã­ch dÃ¹ng chung vÃ  2 FreeRTOS task (publish/poll).
 */

#include "cloud.h"
#include "cloud_internal.h"
#include "config.h"
#include "globals.h"
#include "relay.h"
#include "wifi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "CLOUD"
#define CLOUD_POLL_ERROR_LOG_PERIOD_MS 15000UL

static char s_cloud_init_url[MAX_URL_LEN + 128];
static char s_cloud_init_resp_buf[64];
static char s_cloud_poll_resp_buf[MAX_HTTP_RESPONSE_SIZE];
static char s_cloud_poll_config_buf[CLOUD_CONFIG_CACHE_LEN];
static uint64_t s_last_command_poll_error_log_ms = 0;
static uint64_t s_last_config_poll_error_log_ms = 0;
// Thực hiện xử lý trong cloud_current_timestamp_s.
int64_t cloud_current_timestamp_s(void) {
    time_t now = time(NULL);
    if (now > 946684800) {
        return (int64_t)now;
    }
    return (int64_t)(esp_timer_get_time() / 1000000LL);
}
// Thực hiện xử lý trong cloud_energy_kwh_to_wh.
int64_t cloud_energy_kwh_to_wh(float energy_kwh) {
    if (energy_kwh <= 0.0f) {
        return 0;
    }
    return (int64_t)(((double)energy_kwh * 1000.0) + 0.5);
}
// Thực hiện xử lý trong cloud_round_2dp.
static double cloud_round_2dp(double value) {
    if (!isfinite(value)) {
        return 0.0;
    }

    if (value >= 0.0) {
        return floor(value * 100.0 + 0.5) / 100.0;
    }
    return ceil(value * 100.0 - 0.5) / 100.0;
}
// Đặt lại trạng thái trong cloud_task_wdt_reset_if_subscribed.
void cloud_task_wdt_reset_if_subscribed(void) {
    esp_err_t status = esp_task_wdt_status(NULL);
    if (status == ESP_OK) {
        (void)esp_task_wdt_reset();
    }
}
// Thực hiện xử lý trong cloud_task_delay_with_wdt.
void cloud_task_delay_with_wdt(uint32_t delay_ms) {
    while (delay_ms > 0) {
        uint32_t slice_ms = (delay_ms > 1000U) ? 1000U : delay_ms;
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        cloud_task_wdt_reset_if_subscribed();
        delay_ms -= slice_ms;
    }
}
// Gửi yêu cầu trong cloud_request_busy.
bool cloud_request_busy(esp_err_t err) {
    return err == ESP_ERR_TIMEOUT;
}
// Thực hiện xử lý trong cloud_should_log_periodic.
static bool cloud_should_log_periodic(uint64_t *last_log_ms, uint32_t period_ms) {
    const uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000LL);
    if (*last_log_ms == 0 || (now_ms - *last_log_ms) >= period_ms) {
        *last_log_ms = now_ms;
        return true;
    }
    return false;
}

// HELPERS Ná»˜I Bá»˜
static const char *public_state_to_str(void) {
    charging_session_t session;
    error_code_t error_code = globals_get_error_code();

    globals_get_session_snapshot(&session);

    if (g_system_state == STATE_FAULT || error_code == ERR_SENSOR_ERROR ||
        error_code == ERR_OVER_CURRENT || error_code == ERR_OVER_VOLTAGE ||
        error_code == ERR_UNDER_VOLTAGE || error_code == ERR_RELAY_FAULT) {
        return "FAULT";
    }

    if (relay_is_on() || session.active || g_system_state == STATE_CHARGING) {
        return "CHARGING";
    }

    return "READY";
}

// INIT
// Khởi tạo thành phần trong cloud_init.
void cloud_init(void) {
    cloud_http_init();

    if (!cloud_config_init()) {
        return;
    }

    cloud_config_sync_ntp();
    cloud_config_build_url(s_cloud_init_url, sizeof(s_cloud_init_url), "");

    const wifi_config_data_t *cfg = cloud_config_get();

    if (wifi_is_connected()) {
        if (cloud_http_request(s_cloud_init_url, HTTP_METHOD_GET, NULL,
                               s_cloud_init_resp_buf, sizeof(s_cloud_init_resp_buf),
                               NULL, 0U) == ESP_OK) {
            cloud_config_set_online(true);
            ESP_LOGI(TAG, "Khoi tao cloud OK. DB_URL=%s, DevID=%s",
                     cfg->firebase_url, cfg->device_id);
        } else {
            cloud_config_set_online(false);
            ESP_LOGW(TAG, "Firebase chua truy cap duoc o thoi diem boot.");
        }
    } else {
        cloud_config_set_online(false);
        ESP_LOGI(TAG, "WiFi chua san sang o thoi diem boot, cloud se retry nen.");
    }
}

// CLOUD PUBLISH TASK
// Gửi dữ liệu trong cloud_publish_task.
void cloud_publish_task(void *pvParameters) {
    (void)pvParameters;

    if (!cloud_config_has_config()) {
        vTaskDelete(NULL);
    }

    cloud_config_sync_ntp();

    uint32_t last_heartbeat_second = UINT32_MAX;

    while (1) {
        if (wifi_is_connected()) {
            cJSON *root = cJSON_CreateObject();
            pzem_data_t sensor;
            charging_session_t session;
            char *json_str;

            if (root == NULL) {
                ESP_LOGW(TAG, "Khong tao duoc root JSON de dong bo telemetry.");
                cloud_task_delay_with_wdt(CLOUD_PUBLISH_PERIOD_MS);
                continue;
            }

            globals_get_sensor_data(&sensor);
            globals_get_session_snapshot(&session);

            cloud_report_check_pending();

            cJSON *tm = cJSON_CreateObject();
            const int64_t telemetry_energy_total_wh = cloud_energy_kwh_to_wh(sensor.energy_kwh);
            cJSON_AddNumberToObject(tm, "voltage", cloud_round_2dp((double)sensor.voltage));
            cJSON_AddNumberToObject(tm, "current", cloud_round_2dp((double)sensor.current));
            cJSON_AddNumberToObject(tm, "power", cloud_round_2dp((double)sensor.power));
            cJSON_AddNumberToObject(tm, "energy_total_wh", (double)telemetry_energy_total_wh);
            cJSON_AddItemToObject(root, "telemetry", tm);

            cJSON *st = cJSON_CreateObject();
            cJSON_AddBoolToObject(st, "relay", relay_is_on());
            cJSON_AddStringToObject(st, "state", public_state_to_str());
            cJSON_AddStringToObject(st, "state_detail", system_state_to_str(g_system_state));
            cJSON_AddStringToObject(st, "error_code", error_code_to_str(globals_get_error_code()));
            cJSON_AddItemToObject(root, "status", st);

            if (session.active || session.end_reason != STOP_REASON_NONE) {
                cJSON *sess = cJSON_CreateObject();
                const int64_t session_energy_used_wh = cloud_energy_kwh_to_wh(session.energy_used_kwh);
                cJSON_AddStringToObject(sess, "session_id", session.session_id);
                cJSON_AddStringToObject(sess, "user_id", session.user_id);
                cJSON_AddNumberToObject(sess, "start_time", (double)session.start_time);
                cJSON_AddNumberToObject(sess, "end_time", (double)session.end_time);
                cJSON_AddNumberToObject(sess, "energy_used_wh", (double)session_energy_used_wh);
                cJSON_AddStringToObject(sess, "end_reason", stop_reason_to_str(session.end_reason));
                cJSON_AddItemToObject(root, "current_session", sess);
            }

            if (last_heartbeat_second == UINT32_MAX ||
                (g_uptime_seconds - last_heartbeat_second) >= (HEARTBEAT_PERIOD_MS / 1000U)) {
                cJSON *hb = cJSON_CreateObject();
                cJSON_AddNumberToObject(hb, "last_seen", (double)cloud_current_timestamp_s());
                cJSON_AddNumberToObject(hb, "uptime_seconds", g_uptime_seconds);
                cJSON_AddItemToObject(root, "heartbeat", hb);
                last_heartbeat_second = g_uptime_seconds;
            }

            cJSON *info = cJSON_CreateObject();
            char ip[16];
            wifi_get_ip(ip, sizeof(ip));
            cJSON_AddStringToObject(info, "firmware_version", FIRMWARE_VERSION);
            cJSON_AddStringToObject(info, "ip_address", ip);
            cJSON_AddItemToObject(root, "info", info);

            json_str = cJSON_PrintUnformatted(root);
            if (json_str != NULL) {
                const size_t json_len = strlen(json_str);
                if (json_len >= (MAX_JSON_BUF_SIZE * 2U)) {
                    ESP_LOGW(TAG, "Bo qua dong bo vi payload qua dai (%lu bytes).",
                             (unsigned long)json_len);
                } else {
                    esp_err_t publish_err = cloud_http_firebase_patch("", json_str);
                    if (publish_err == ESP_OK) {
                        cloud_config_set_online(true);
                    } else if (!cloud_request_busy(publish_err)) {
                        cloud_config_set_online(false);
                    } else {
                        ESP_LOGD(TAG, "Bo qua mot nhip dong bo vi cloud mutex dang ban.");
                    }
                }
                free(json_str);
            } else {
                ESP_LOGW(TAG, "Khong the chuyen JSON thanh chuoi de dong bo.");
            }

            cJSON_Delete(root);
        }

        cloud_task_delay_with_wdt(CLOUD_PUBLISH_PERIOD_MS);
    }
}

// CLOUD POLL TASK
// Chạy tác vụ trong cloud_poll_task.
void cloud_poll_task(void *pvParameters) {
    (void)pvParameters;

    if (!cloud_config_has_config()) {
        vTaskDelete(NULL);
    }

    cloud_config_sync_ntp();

    memset(s_cloud_poll_resp_buf, 0, sizeof(s_cloud_poll_resp_buf));
    uint32_t config_poll_divider = 0;

    while (1) {
        if (wifi_is_connected()) {
            bool any_request_ok = false;
            bool saw_real_failure = false;

            esp_err_t err = cloud_http_firebase_get("/command",
                                                    s_cloud_poll_resp_buf,
                                                    sizeof(s_cloud_poll_resp_buf));

            if (err == ESP_OK) {
                any_request_ok = true;
                cloud_command_process_response(s_cloud_poll_resp_buf);
            } else if (!cloud_request_busy(err)) {
                saw_real_failure = true;
                if (cloud_should_log_periodic(&s_last_command_poll_error_log_ms,
                                              CLOUD_POLL_ERROR_LOG_PERIOD_MS)) {
                    ESP_LOGW(TAG, "Khong the doc node /command tu Firebase.");
                }
            }

            if (config_poll_divider == 0U) {
                esp_err_t cfg_err = cloud_http_firebase_get("/config",
                                                            s_cloud_poll_config_buf,
                                                            sizeof(s_cloud_poll_config_buf));

                if (cfg_err == ESP_OK) {
                    any_request_ok = true;
                    cloud_command_apply_config_response(s_cloud_poll_config_buf);
                } else if (!cloud_request_busy(cfg_err)) {
                    saw_real_failure = true;
                    if (cloud_should_log_periodic(&s_last_config_poll_error_log_ms,
                                                  CLOUD_POLL_ERROR_LOG_PERIOD_MS)) {
                        ESP_LOGW(TAG, "Khong the doc node /config tu Firebase.");
                    }
                }
            }

            config_poll_divider = (config_poll_divider + 1U) % CLOUD_CONFIG_POLL_DIVIDER;

            if (any_request_ok) {
                cloud_config_set_online(true);
            } else if (saw_real_failure) {
                cloud_config_set_online(false);
            }
        }

        cloud_task_delay_with_wdt(CLOUD_POLL_PERIOD_MS);
    }
}



