/**
 * @file cloud_command.c
 * @brief Parse + dispatch lá»‡nh tá»« Firebase vÃ  Ã¡p dá»¥ng remote config.
 */

#include "cloud.h"
#include "cloud_internal.h"
#include "config.h"
#include "globals.h"
#include "nvs_manager.h"
#include "ota.h"
#include "relay.h"
#include "types.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "CLOUD_CMD"

static char s_last_processed_command_id[MAX_COMMAND_ID_LEN] = { 0 };
static char s_last_config_payload[CLOUD_CONFIG_CACHE_LEN]   = { 0 };
static bool s_config_payload_cached                          = false;

// DEDUP COMMAND ID
// Thực hiện xử lý trong remember_processed_command_id.
static void remember_processed_command_id(const char *command_id) {
    if (command_id == NULL || command_id[0] == '\0') {
        return;
    }
    strncpy(s_last_processed_command_id, command_id, sizeof(s_last_processed_command_id) - 1);
    s_last_processed_command_id[sizeof(s_last_processed_command_id) - 1] = '\0';
}
// Thực hiện xử lý trong command_id_is_duplicate.
static bool command_id_is_duplicate(const char *command_id) {
    return (command_id != NULL &&
            command_id[0] != '\0' &&
            strcmp(s_last_processed_command_id, command_id) == 0);
}

// COMMAND PARSING
// Thực hiện xử lý trong command_from_str.
static firebase_command_t command_from_str(const char *cmd_str) {
    if (cmd_str == NULL || strcmp(cmd_str, "NONE") == 0)       return CMD_NONE;
    if (strcmp(cmd_str, "START_CHARGING") == 0)                return CMD_START_CHARGING;
    if (strcmp(cmd_str, "STOP_CHARGING") == 0)                 return CMD_STOP_CHARGING;
    if (strcmp(cmd_str, "RESET_DEVICE") == 0)                  return CMD_RESET_DEVICE;
    if (strcmp(cmd_str, "RESET_STATION") == 0)                 return CMD_RESET_STATION;
    if (strcmp(cmd_str, "RELAY_ON") == 0)                      return CMD_RELAY_ON;
    if (strcmp(cmd_str, "RELAY_OFF") == 0)                     return CMD_RELAY_OFF;
    if (strcmp(cmd_str, "UPDATE_FIRMWARE") == 0)               return CMD_UPDATE_FIRMWARE;
    if (strcmp(cmd_str, "UPDATE_CONFIG") == 0)                 return CMD_UPDATE_CONFIG;
    return CMD_INVALID;
}
// Thực hiện xử lý trong populate_default_command_fields.
static void populate_default_command_fields(station_command_t *command) {
    if (command->session_id[0] == '\0') {
        snprintf(command->session_id, sizeof(command->session_id),
                 "sess_%lld", (long long)cloud_current_timestamp_s());
    }

    if (command->user_id[0] == '\0') {
        if (command->issued_by[0] != '\0') {
            strncpy(command->user_id, command->issued_by, sizeof(command->user_id) - 1);
        } else {
            strncpy(command->user_id, "cloud_user", sizeof(command->user_id) - 1);
        }
        command->user_id[sizeof(command->user_id) - 1] = '\0';
    }

    if (command->max_current <= 0.0f) {
        command->max_current = globals_get_max_current_limit();
    }
}
// Thực hiện xử lý trong extract_max_current_from_object.
static bool extract_max_current_from_object(cJSON *obj, float *out_max_current) {
    cJSON *max_current = NULL;

    if (obj == NULL || out_max_current == NULL || !cJSON_IsObject(obj)) {
        return false;
    }

    max_current = cJSON_GetObjectItem(obj, "maxCurrent");
    if (!cJSON_IsNumber(max_current)) {
        max_current = cJSON_GetObjectItem(obj, "max_current");
    }
    if (!cJSON_IsNumber(max_current)) {
        return false;
    }

    *out_max_current = (float)max_current->valuedouble;
    return true;
}
// Thực hiện xử lý trong object_has_max_current_field.
static bool object_has_max_current_field(cJSON *obj) {
    if (obj == NULL || !cJSON_IsObject(obj)) {
        return false;
    }

    return cJSON_HasObjectItem(obj, "maxCurrent") || cJSON_HasObjectItem(obj, "max_current");
}
// Áp dụng cấu hình trong apply_runtime_max_current.
static bool apply_runtime_max_current(float requested_current, const char *source) {
    if (!isfinite(requested_current)) {
        ESP_LOGW(TAG, "Bo qua maxCurrent khong hop le tu %s.", (source != NULL) ? source : "<unknown>");
        return false;
    }

    const float previous_current = globals_get_max_current_limit();
    float clamped_current = requested_current;
    if (clamped_current < REMOTE_MAX_CURRENT_MIN_A) {
        clamped_current = REMOTE_MAX_CURRENT_MIN_A;
    } else if (clamped_current > REMOTE_MAX_CURRENT_MAX_A) {
        clamped_current = REMOTE_MAX_CURRENT_MAX_A;
    }

    if (requested_current != clamped_current) {
        ESP_LOGW(TAG, "Clamp maxCurrent tu %.3fA ve %.3fA (%s).",
                 requested_current, clamped_current, (source != NULL) ? source : "<unknown>");
    }

    if (!nvs_save_runtime_max_current_limit(clamped_current)) {
        ESP_LOGW(TAG, "Khong the luu runtime maxCurrent %.3fA vao NVS.", clamped_current);
    }

    if (fabsf(previous_current - clamped_current) < 0.0001f) {
        globals_set_active_session_max_current_limit(clamped_current);
        ESP_LOGI(TAG, "maxCurrent runtime giu nguyen %.3fA (%s).",
                 clamped_current, (source != NULL) ? source : "<unknown>");
        return true;
    }

    globals_set_max_current_limit(clamped_current);
    globals_set_active_session_max_current_limit(clamped_current);

    ESP_LOGI(TAG, "Cap nhat maxCurrent runtime: %.3fA -> %.3fA (%s).",
             previous_current, clamped_current, (source != NULL) ? source : "<unknown>");
    return true;
}
// Thực hiện xử lý trong acknowledge_object_command.
static void acknowledge_object_command(const char *status) {
    char payload[96];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
    if (cloud_http_firebase_patch("/command", payload) != ESP_OK) {
        ESP_LOGW(TAG, "Khong the cap nhat status cho command object.");
    }
}
// Thực hiện xử lý trong consume_string_command.
static void consume_string_command(void) {
    if (cloud_http_firebase_put_string("/command", "NONE") != ESP_OK) {
        ESP_LOGW(TAG, "Khong the dat lai chuoi command ve NONE.");
    }
}
// Áp dụng cấu hình trong apply_remote_config.
static bool apply_remote_config(cJSON *root) {
    cJSON *config = root;
    float max_current_value = 0.0f;

    if (root == NULL || !cJSON_IsObject(root)) {
        return true;
    }

    cJSON *nested_config = cJSON_GetObjectItem(root, "config");
    if (cJSON_IsObject(nested_config)) {
        config = nested_config;
    }

    if (extract_max_current_from_object(config, &max_current_value)) {
        apply_runtime_max_current(max_current_value, "/config");
    } else if (object_has_max_current_field(config)) {
        ESP_LOGW(TAG, "Bo qua /config vi maxCurrent khong hop le.");
    }
    return true;
}
// Phân tích dữ liệu trong parse_command_payload.
static firebase_command_t parse_command_payload(cJSON *command_item,
                                                station_command_t *st_cmd,
                                                bool *command_is_object) {
    firebase_command_t command = CMD_NONE;

    if (command_is_object != NULL) {
        *command_is_object = false;
    }

    if (command_item == NULL) {
        return CMD_NONE;
    }

    if (cJSON_IsString(command_item) && command_item->valuestring != NULL) {
        return command_from_str(command_item->valuestring);
    }

    if (!cJSON_IsObject(command_item)) {
        return CMD_NONE;
    }

    if (command_is_object != NULL) {
        *command_is_object = true;
    }

    cJSON *cmd_cfg          = cJSON_GetObjectItem(command_item, "config");
    cJSON *command_id       = cJSON_GetObjectItem(command_item, "id");
    cJSON *issued_by        = cJSON_GetObjectItem(command_item, "issued_by");
    cJSON *timestamp        = cJSON_GetObjectItem(command_item, "timestamp");
    cJSON *expire_at        = cJSON_GetObjectItem(command_item, "expire_at");
    cJSON *cmd              = cJSON_GetObjectItem(command_item, "cmd");
    cJSON *type_field       = cJSON_GetObjectItem(command_item, "type");
    cJSON *status           = cJSON_GetObjectItem(command_item, "status");
    cJSON *session_id       = cJSON_GetObjectItem(command_item, "session_id");
    cJSON *user_id          = cJSON_GetObjectItem(command_item, "user_id");
    cJSON *relay            = cJSON_GetObjectItem(command_item, "relay");
    cJSON *session_id_cfg   = (cmd_cfg != NULL) ? cJSON_GetObjectItem(cmd_cfg, "sessionId") : NULL;
    cJSON *target_energy_wh = (cmd_cfg != NULL) ? cJSON_GetObjectItem(cmd_cfg, "targetEnergyWh") : NULL;
    cJSON *target_energy_kwh= (cmd_cfg != NULL) ? cJSON_GetObjectItem(cmd_cfg, "targetEnergyKwh") : NULL;
    cJSON *price_per_kwh    = (cmd_cfg != NULL) ? cJSON_GetObjectItem(cmd_cfg, "pricePerKwh") : NULL;
    cJSON *relay_cfg        = (cmd_cfg != NULL) ? cJSON_GetObjectItem(cmd_cfg, "relay") : NULL;
    float max_current_cfg   = 0.0f;

    if (cJSON_IsString(status) && status->valuestring != NULL &&
        strcmp(status->valuestring, "NEW") != 0 &&
        strcmp(status->valuestring, "PENDING") != 0) {
        if (cJSON_IsString(command_id) && command_id->valuestring != NULL) {
            remember_processed_command_id(command_id->valuestring);
        }
        return CMD_NONE;
    }

    if (cJSON_IsString(command_id) && command_id->valuestring != NULL &&
        command_id_is_duplicate(command_id->valuestring)) {
        ESP_LOGD(TAG, "Bo qua command trung id=%s", command_id->valuestring);
        return CMD_NONE;
    }

    if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
        command = command_from_str(cmd->valuestring);
    } else if (cJSON_IsString(type_field) && type_field->valuestring != NULL) {
        command = command_from_str(type_field->valuestring);
    }

    if (cJSON_IsString(command_id) && command_id->valuestring != NULL) {
        strncpy(st_cmd->command_id, command_id->valuestring, sizeof(st_cmd->command_id) - 1);
    }

    if (cJSON_IsString(issued_by) && issued_by->valuestring != NULL) {
        strncpy(st_cmd->issued_by, issued_by->valuestring, sizeof(st_cmd->issued_by) - 1);
    }

    if (cJSON_IsNumber(timestamp)) {
        st_cmd->timestamp = (int64_t)timestamp->valuedouble;
    }

    if (cJSON_IsNumber(expire_at)) {
        st_cmd->expire_at = (int64_t)expire_at->valuedouble;
        if (st_cmd->expire_at > 0) {
            time_t now = time(NULL);
            if (now > 946684800 && (int64_t)now > st_cmd->expire_at) {
                if (cJSON_IsString(command_id) && command_id->valuestring != NULL) {
                    remember_processed_command_id(command_id->valuestring);
                }
                acknowledge_object_command("EXPIRED");
                return CMD_NONE;
            }
        }
    }

    if (command == CMD_NONE) {
        return CMD_NONE;
    }

    if (cJSON_IsString(session_id_cfg) && session_id_cfg->valuestring != NULL) {
        strncpy(st_cmd->session_id, session_id_cfg->valuestring, sizeof(st_cmd->session_id) - 1);
    } else if (cJSON_IsString(session_id) && session_id->valuestring != NULL) {
        strncpy(st_cmd->session_id, session_id->valuestring, sizeof(st_cmd->session_id) - 1);
    }

    if (cJSON_IsString(user_id) && user_id->valuestring != NULL) {
        strncpy(st_cmd->user_id, user_id->valuestring, sizeof(st_cmd->user_id) - 1);
    } else if (st_cmd->issued_by[0] != '\0') {
        strncpy(st_cmd->user_id, st_cmd->issued_by, sizeof(st_cmd->user_id) - 1);
    }

    if (cJSON_IsNumber(target_energy_wh)) {
        st_cmd->target_energy_wh = (int)target_energy_wh->valuedouble;
    } else if (cJSON_IsNumber(target_energy_kwh)) {
        st_cmd->target_energy_wh = (int)(((double)target_energy_kwh->valuedouble * 1000.0) + 0.5);
    }

    if (st_cmd->target_energy_wh > 0) {
        st_cmd->target_energy_kwh = ((float)st_cmd->target_energy_wh) / 1000.0f;
    } else {
        st_cmd->target_energy_kwh = 0.0f;
    }

    if (cJSON_IsNumber(price_per_kwh)) {
        st_cmd->price_per_kwh = (int)price_per_kwh->valuedouble;
    }

    if (cJSON_IsBool(relay_cfg)) {
        st_cmd->relay = cJSON_IsTrue(relay_cfg);
    } else if (cJSON_IsBool(relay)) {
        st_cmd->relay = cJSON_IsTrue(relay);
    }

    if (extract_max_current_from_object(cmd_cfg, &max_current_cfg) ||
        extract_max_current_from_object(command_item, &max_current_cfg)) {
        st_cmd->max_current = max_current_cfg;
    }

    return command;
}

// COMMAND HANDLER
// Xử lý sự kiện trong handle_command.
static bool handle_command(firebase_command_t command,
                           station_command_t *st_cmd,
                           bool command_is_object) {
    char msg[128];

    switch (command) {
        case CMD_NONE:
            return false;

        case CMD_START_CHARGING:
        case CMD_RELAY_ON:
            if (command == CMD_RELAY_ON) {
                ESP_LOGW(TAG, "Lenh RELAY_ON cu duoc xu ly nhu START_CHARGING.");
            }

            if (command_is_object && st_cmd->session_id[0] == '\0') {
                ESP_LOGW(TAG, "Tu choi START_CHARGING vi thieu sessionId trong command object.");
                acknowledge_object_command("REJECTED_INVALID");
                return true;
            }
            populate_default_command_fields(st_cmd);

            if (g_system_state == STATE_READY) {
                if (globals_begin_session(st_cmd, msg, sizeof(msg))) {
                    const float target_kwh =
                        (st_cmd->target_energy_kwh > 0.0f)
                            ? st_cmd->target_energy_kwh
                            : ((st_cmd->target_energy_wh > 0) ? ((float)st_cmd->target_energy_wh / 1000.0f) : 0.0f);
                    ESP_LOGI(TAG,
                             "Bat relay | cmd=%s id=%s session=%s user=%s max_current=%.2fA target=%.3fkWh price=%d",
                             (command == CMD_RELAY_ON) ? "RELAY_ON" : "START_CHARGING",
                             st_cmd->command_id, st_cmd->session_id, st_cmd->user_id,
                             st_cmd->max_current, target_kwh, st_cmd->price_per_kwh);
                    cloud_report_reset_last_session_id();
                    relay_on();
                    globals_set_error_code(ERR_NONE);
                    globals_set_system_state(STATE_CHARGING);
                    globals_set_active_session_max_current_limit(st_cmd->max_current);
                    if (command_is_object) {
                        acknowledge_object_command("ACCEPTED");
                    } else {
                        consume_string_command();
                    }
                    beep(1, 200);
                } else {
                    if (command_is_object) {
                        acknowledge_object_command("REJECTED_BUSY");
                    } else {
                        consume_string_command();
                    }
                }
            } else if (command_is_object) {
                acknowledge_object_command("REJECTED_DEVICE_NOT_READY");
            } else {
                consume_string_command();
            }
            return true;

        case CMD_STOP_CHARGING:
        case CMD_RELAY_OFF: {
            if (command == CMD_RELAY_OFF) {
                ESP_LOGW(TAG, "Lenh RELAY_OFF cu duoc xu ly nhu STOP_CHARGING.");
            }
            charging_session_t session;
            const bool charging_state = (g_system_state == STATE_CHARGING || g_system_state == STATE_OFFLINE);
            const bool relay_active   = relay_is_on();
            bool session_finished     = false;

            globals_get_session_snapshot(&session);
            if (session.active || charging_state || relay_active) {
                relay_off();
                globals_set_error_code(ERR_NONE);

                if (session.active) {
                    session_finished = globals_finish_session(STOP_REASON_USER_STOP,
                                                              msg, sizeof(msg));
                }

                if (g_system_state != STATE_FAULT) {
                    globals_set_system_state(
                        (globals_get_network_status() == NET_ONLINE) ? STATE_READY : STATE_IDLE);
                }
                if (session.active && !session_finished) {
                    ESP_LOGW(TAG, "Lenh STOP da tat relay nhung chua chot duoc session active.");
                }

                if (command_is_object) {
                    acknowledge_object_command("ACCEPTED");
                } else {
                    consume_string_command();
                }
                beep(2, 200);
            } else if (command_is_object) {
                acknowledge_object_command("REJECTED_NOT_CHARGING");
            } else {
                consume_string_command();
            }
            return true;
        }

        case CMD_RESET_DEVICE:
        case CMD_RESET_STATION:
            relay_off();
            nvs_clear_fault_latch();
            globals_set_error_code(ERR_NONE);
            if (globals_is_session_active()) {
                if (globals_finish_session(STOP_REASON_STATION_FAULT, msg, sizeof(msg))) {
                    if (!cloud_report_session_end(STOP_REASON_STATION_FAULT, CHARGE_RESULT_WARNING)) {
                        ESP_LOGW(TAG, "Khong the gui report RESET_STATION len backend.");
                    }
                }
            }
            if (command_is_object) {
                acknowledge_object_command("ACCEPTED");
            } else {
                consume_string_command();
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
            return true;

        case CMD_UPDATE_FIRMWARE: {
            charging_session_t session;
            const bool charging_state = (g_system_state == STATE_CHARGING || g_system_state == STATE_OFFLINE);
            const bool relay_active   = relay_is_on();

            globals_get_session_snapshot(&session);
            if (session.active || charging_state || relay_active) {
                ESP_LOGW(TAG, "Tu choi OTA vi tram dang sac hoac co session active.");
                if (command_is_object) {
                    acknowledge_object_command("REJECTED_BUSY");
                } else {
                    consume_string_command();
                }
                return true;
            }

            if (!ota_request_start()) {
                ESP_LOGW(TAG, "Tu choi OTA command vi OTA dang chay hoac khong tao duoc tac vu.");
                if (command_is_object) {
                    acknowledge_object_command("REJECTED_BUSY");
                } else {
                    consume_string_command();
                }
                return true;
            }

            if (command_is_object) {
                acknowledge_object_command("ACCEPTED");
            } else {
                consume_string_command();
            }
            return true;
        }

        case CMD_UPDATE_CONFIG:
            if (st_cmd->max_current > 0.0f && isfinite(st_cmd->max_current)) {
                apply_runtime_max_current(st_cmd->max_current, "UPDATE_CONFIG");
            } else {
                ESP_LOGI(TAG, "Nhan UPDATE_CONFIG khong kem maxCurrent hop le, doi dong bo /config.");
            }

            if (command_is_object) {
                acknowledge_object_command("ACCEPTED");
            } else {
                consume_string_command();
            }
            return true;

        case CMD_INVALID:
        default:
            if (command_is_object) {
                acknowledge_object_command("REJECTED_INVALID");
            } else {
                consume_string_command();
            }
            return true;
    }
}

// CONFIG PAYLOAD DEDUP
// Thực hiện xử lý trong config_payload_is_unchanged.
static bool config_payload_is_unchanged(const char *payload) {
    return payload != NULL &&
           s_config_payload_cached &&
           strcmp(s_last_config_payload, payload) == 0;
}
// Thực hiện xử lý trong remember_config_payload.
static void remember_config_payload(const char *payload) {
    if (payload == NULL || payload[0] == '\0') {
        s_last_config_payload[0] = '\0';
        s_config_payload_cached = false;
        return;
    }

    strncpy(s_last_config_payload, payload, sizeof(s_last_config_payload) - 1);
    s_last_config_payload[sizeof(s_last_config_payload) - 1] = '\0';
    s_config_payload_cached = true;
}

// PUBLIC API
// Thực hiện xử lý trong cloud_command_process_response.
void cloud_command_process_response(const char *resp_json) {
    if (resp_json == NULL ||
        resp_json[0] == '\0' ||
        strcmp(resp_json, "null") == 0) {
        return;
    }

    cJSON *command_root = cJSON_Parse(resp_json);
    if (command_root == NULL) {
        ESP_LOGW(TAG, "JSON command tu Firebase khong phan tich duoc.");
        return;
    }

    station_command_t st_cmd = { 0 };
    bool command_is_object = false;
    firebase_command_t command = parse_command_payload(command_root, &st_cmd, &command_is_object);

    if (command != CMD_NONE) {
        st_cmd.command = command;
        if (handle_command(command, &st_cmd, command_is_object) &&
            command_is_object && st_cmd.command_id[0] != '\0') {
            remember_processed_command_id(st_cmd.command_id);
        }
    }

    cJSON_Delete(command_root);
}
// Áp dụng cấu hình trong cloud_command_apply_config_response.
void cloud_command_apply_config_response(const char *resp_json) {
    if (resp_json == NULL || resp_json[0] == '\0' || strcmp(resp_json, "null") == 0) {
        remember_config_payload(NULL);
        return;
    }

    if (config_payload_is_unchanged(resp_json)) {
        return;
    }

    cJSON *config_root = cJSON_Parse(resp_json);
    if (config_root == NULL) {
        ESP_LOGW(TAG, "JSON config tu Firebase khong phan tich duoc.");
        return;
    }

    const bool config_applied = apply_remote_config(config_root);
    if (config_applied) {
        remember_config_payload(resp_json);
    }
    cJSON_Delete(config_root);
}


