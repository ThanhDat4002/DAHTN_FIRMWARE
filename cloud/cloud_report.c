/**
 * @file cloud_report.c
 * @brief Gá»­i session report lÃªn backend web.
 */

#include "cloud.h"
#include "cloud_internal.h"
#include "config.h"
#include "globals.h"
#include "nvs_manager.h"
#include "types.h"
#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "CLOUD_REPORT"
#define REPORT_RETRY_LOG_PERIOD_MS 30000UL

static char s_last_reported_session_id[MAX_SESSION_ID_LEN] = { 0 };
static uint64_t s_last_report_retry_log_ms = 0;

// HELPERS
static const char *backend_report_status_str(charge_result_status_t result_status) {
    switch (result_status) {
        case CHARGE_RESULT_SUCCESS: return "Success";
        case CHARGE_RESULT_WARNING: return "Warning";
        case CHARGE_RESULT_ERROR:
        default:                    return "Error";
    }
}
// Kiểm tra điều kiện trong should_log_report_retry.
static bool should_log_report_retry(void) {
    const uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    if (s_last_report_retry_log_ms == 0 ||
        (now_ms - s_last_report_retry_log_ms) >= REPORT_RETRY_LOG_PERIOD_MS) {
        s_last_report_retry_log_ms = now_ms;
        return true;
    }
    return false;
}

static const char *stop_reason_for_report(stop_reason_t reason) {
    switch (reason) {
        case STOP_REASON_USER_STOP:        return "user_stop";
        case STOP_REASON_FULL_CHARGE:      return "completed";
        case STOP_REASON_OVERLOAD:         return "overload";
        case STOP_REASON_UNPLUGGED:        return "unplugged";
        case STOP_REASON_CONNECTION_ERROR: return "connection_error";
        case STOP_REASON_STATION_FAULT:    return "station_fault";
        case STOP_REASON_COMPLETED:        return "completed";
        case STOP_REASON_NONE:
        default:                           return "completed";
    }
}
// Thực hiện xử lý trong infer_result_status_from_reason.
static charge_result_status_t infer_result_status_from_reason(stop_reason_t reason) {
    switch (reason) {
        case STOP_REASON_USER_STOP:
        case STOP_REASON_FULL_CHARGE:
        case STOP_REASON_COMPLETED:
            return CHARGE_RESULT_SUCCESS;
        case STOP_REASON_UNPLUGGED:
        case STOP_REASON_CONNECTION_ERROR:
            return CHARGE_RESULT_WARNING;
        case STOP_REASON_OVERLOAD:
        case STOP_REASON_STATION_FAULT:
        default:
            return CHARGE_RESULT_ERROR;
    }
}
// Thực hiện xử lý trong response_is_already_processed.
static bool response_is_already_processed(const char *resp_json) {
    if (resp_json == NULL || resp_json[0] == '\0') {
        return false;
    }

    cJSON *root = cJSON_Parse(resp_json);
    if (root == NULL) {
        return false;
    }

    bool already_processed = false;
    cJSON *flag = cJSON_GetObjectItemCaseSensitive(root, "alreadyProcessed");
    if (cJSON_IsBool(flag)) {
        already_processed = cJSON_IsTrue(flag);
    } else {
        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (cJSON_IsObject(data)) {
            cJSON *nested_flag = cJSON_GetObjectItemCaseSensitive(data, "alreadyProcessed");
            if (cJSON_IsBool(nested_flag)) {
                already_processed = cJSON_IsTrue(nested_flag);
            }
        }
    }

    cJSON_Delete(root);
    return already_processed;
}

// PUBLIC API
// Gửi dữ liệu trong cloud_report_was_session_reported.
bool cloud_report_was_session_reported(const char *session_id) {
    return (session_id != NULL &&
            session_id[0] != '\0' &&
            strcmp(s_last_reported_session_id, session_id) == 0);
}
// Đặt lại trạng thái trong cloud_report_reset_last_session_id.
void cloud_report_reset_last_session_id(void) {
    s_last_reported_session_id[0] = '\0';
}
// Gửi dữ liệu trong cloud_report_session_end.
bool cloud_report_session_end(stop_reason_t reason, charge_result_status_t result_status) {
    if (!wifi_is_connected() || !cloud_config_has_config()) {
        return false;
    }

    charging_session_t session;
    globals_get_session_snapshot(&session);

    if (session.session_id[0] == '\0' || session.end_reason == STOP_REASON_NONE) {
        return false;
    }

    if (cloud_report_was_session_reported(session.session_id)) {
        return true;
    }

    char report_url[MAX_URL_LEN + 64];
    char json_payload[256];
    char resp_buf[256] = { 0 };

    const char *status_text = backend_report_status_str(result_status);
    const char *stop_reason =
        stop_reason_for_report(reason != STOP_REASON_NONE ? reason : session.end_reason);
    const int energy_wh = (int)cloud_energy_kwh_to_wh(session.energy_used_kwh);
    const wifi_config_data_t *cfg = cloud_config_get();

    snprintf(report_url, sizeof(report_url), "%s/api/station/report", BACKEND_API_BASE_URL);
    snprintf(json_payload, sizeof(json_payload),
             "{\"stationId\":\"%s\",\"sessionId\":\"%s\",\"energyWh\":%d,\"status\":\"%s\",\"stopReason\":\"%s\"}",
             cfg->device_id, session.session_id, energy_wh, status_text, stop_reason);

    ESP_LOGI(TAG, "Gui report session %s len backend (%s, %d Wh, %s).",
             session.session_id, status_text, energy_wh, stop_reason);

    esp_err_t report_err = cloud_http_request(report_url, HTTP_METHOD_POST, json_payload,
                                              resp_buf, sizeof(resp_buf),
                                              NULL, 0U);

    const bool already_processed =
        (report_err != ESP_OK) &&
        (resp_buf[0] != '\0') &&
        response_is_already_processed(resp_buf);

    if (report_err == ESP_OK || already_processed) {
        if (report_err != ESP_OK) {
            ESP_LOGW(TAG, "Backend bao alreadyProcessed cho session %s.", session.session_id);
        }
        strncpy(s_last_reported_session_id, session.session_id,
                sizeof(s_last_reported_session_id) - 1);
        s_last_reported_session_id[sizeof(s_last_reported_session_id) - 1] = '\0';
        if (!session.active) {
            nvs_clear_session();
            if (g_system_state == STATE_STOPPED) {
                globals_set_system_state(
                    (globals_get_network_status() == NET_ONLINE) ? STATE_READY : STATE_IDLE);
            }
        }
        return true;
    }

    ESP_LOGW(TAG, "Gui report session that bai: err=%s, resp='%s'",
             esp_err_to_name(report_err),
             (resp_buf[0] != '\0') ? resp_buf : "<empty>");

    return false;
}
// Gửi dữ liệu trong cloud_report_check_pending.
void cloud_report_check_pending(void) {
    charging_session_t session;
    globals_get_session_snapshot(&session);

    if (session.active ||
        session.end_reason == STOP_REASON_NONE ||
        session.session_id[0] == '\0' ||
        cloud_report_was_session_reported(session.session_id)) {
        return;
    }

    if (!cloud_report_session_end(session.end_reason,
                                  infer_result_status_from_reason(session.end_reason))) {
        if (should_log_report_retry()) {
            ESP_LOGW(TAG, "Chua gui duoc report cho session %s, se thu lai o vong sau.",
                     session.session_id);
        }
    }
}


