
#include "ota.h"
#include "ota_firebase.h"
#include "config.h"
#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "OTA"
#define OTA_TASK_STACK_SIZE_BYTES 16384
#define OTA_TASK_PRIORITY         5

static TaskHandle_t       s_ota_task_handle = NULL;
static volatile bool      s_ota_in_progress = false;
static portMUX_TYPE       s_ota_state_lock  = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    int major;
    int minor;
    int patch;
} ota_semver_t;

// SEMVER HELPERS
// Thực hiện xử lý trong ota_version_is_numeric.
static bool ota_version_is_numeric(const char *value) {
    if (value == NULL) {
        return false;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        ++value;
    }

    size_t len = 0;
    const char *p = value;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
        ++len;
        ++p;
    }

    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p != '\0') {
        return false;
    }

    return len >= 10U;
}
// Phân tích dữ liệu trong ota_parse_semver.
static bool ota_parse_semver(const char *value, ota_semver_t *out) {
    if (value == NULL || out == NULL) {
        return false;
    }

    while (*value != '\0' && isspace((unsigned char)*value)) {
        ++value;
    }

    if (*value == 'v' || *value == 'V') {
        ++value;
    }

    if (!isdigit((unsigned char)*value)) {
        return false;
    }

    char *end_ptr = NULL;
    long major = strtol(value, &end_ptr, 10);
    if (end_ptr == NULL || *end_ptr != '.') {
        return false;
    }

    value = end_ptr + 1;
    if (!isdigit((unsigned char)*value)) {
        return false;
    }
    long minor = strtol(value, &end_ptr, 10);
    if (end_ptr == NULL || *end_ptr != '.') {
        return false;
    }

    value = end_ptr + 1;
    if (!isdigit((unsigned char)*value)) {
        return false;
    }
    long patch = strtol(value, &end_ptr, 10);
    if (end_ptr == NULL) {
        return false;
    }

    while (*end_ptr != '\0' && isspace((unsigned char)*end_ptr)) {
        ++end_ptr;
    }
    if (*end_ptr != '\0') {
        return false;
    }

    if (major < 0 || minor < 0 || patch < 0) {
        return false;
    }

    out->major = (int)major;
    out->minor = (int)minor;
    out->patch = (int)patch;
    return true;
}
// So sánh dữ liệu trong ota_compare_semver.
static int ota_compare_semver(const ota_semver_t *left, const ota_semver_t *right) {
    if (left->major != right->major) {
        return (left->major > right->major) ? 1 : -1;
    }
    if (left->minor != right->minor) {
        return (left->minor > right->minor) ? 1 : -1;
    }
    if (left->patch != right->patch) {
        return (left->patch > right->patch) ? 1 : -1;
    }
    return 0;
}

// STATE
// Đánh dấu trạng thái trong ota_mark_idle.
static void ota_mark_idle(void) {
    taskENTER_CRITICAL(&s_ota_state_lock);
    s_ota_in_progress = false;
    s_ota_task_handle = NULL;
    taskEXIT_CRITICAL(&s_ota_state_lock);
}

// IMAGE VALIDATION
// Thực hiện xử lý trong validate_image_header.
static esp_err_t validate_image_header(const esp_app_desc_t *new_app_info,
                                       const char *target_version,
                                       char *reason_buf,
                                       size_t reason_buf_len) {
    if (reason_buf != NULL && reason_buf_len > 0) {
        reason_buf[0] = '\0';
    }

    if (new_app_info == NULL) {
        if (reason_buf != NULL && reason_buf_len > 0) {
            snprintf(reason_buf, reason_buf_len, "invalid_image_descriptor");
        }
        return ESP_ERR_INVALID_ARG;
    }

    if (new_app_info->version[0] == '\0' || new_app_info->project_name[0] == '\0') {
        ESP_LOGE(TAG, "Header firmware moi thieu ten du an hoac phien ban.");
        if (reason_buf != NULL && reason_buf_len > 0) {
            snprintf(reason_buf, reason_buf_len, "invalid_image_header");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    ota_semver_t new_semver = {0};
    const bool new_has_semver = ota_parse_semver(new_app_info->version, &new_semver);

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Firmware dang chay co phien ban: %s", running_app_info.version);

        if (strcmp(new_app_info->project_name, running_app_info.project_name) != 0) {
            ESP_LOGE(TAG, "OTA lech ten du an (moi=%s, dang chay=%s).",
                     new_app_info->project_name, running_app_info.project_name);
            if (reason_buf != NULL && reason_buf_len > 0) {
                snprintf(reason_buf, reason_buf_len, "project_name_mismatch");
            }
            return ESP_FAIL;
        }

        ota_semver_t running_semver = {0};
        const bool running_has_semver = ota_parse_semver(running_app_info.version, &running_semver);
        if (new_has_semver && running_has_semver) {
            const int semver_cmp = ota_compare_semver(&new_semver, &running_semver);
            if (semver_cmp == 0) {
                ESP_LOGW(TAG, "Bo qua OTA vi phien ban semver %s da dang chay.",
                         running_app_info.version);
                if (reason_buf != NULL && reason_buf_len > 0) {
                    snprintf(reason_buf, reason_buf_len, "same_version");
                }
                return ESP_FAIL;
            }
            if (semver_cmp < 0) {
                ESP_LOGE(TAG, "Tu choi OTA ha cap (moi=%s, dang chay=%s).",
                         new_app_info->version, running_app_info.version);
                if (reason_buf != NULL && reason_buf_len > 0) {
                    snprintf(reason_buf, reason_buf_len, "downgrade_not_allowed");
                }
                return ESP_FAIL;
            }
        } else if (strcmp(new_app_info->version, running_app_info.version) == 0) {
            ESP_LOGW(TAG, "Bo qua OTA vi phien ban %s da dang chay.",
                     running_app_info.version);
            if (reason_buf != NULL && reason_buf_len > 0) {
                snprintf(reason_buf, reason_buf_len, "same_version");
            }
            return ESP_FAIL;
        }
    }

    if (target_version != NULL && target_version[0] != '\0') {
        ota_semver_t target_semver = {0};
        const bool target_has_semver = ota_parse_semver(target_version, &target_semver);
        if (target_has_semver) {
            bool matched = false;
            if (new_has_semver) {
                matched = (ota_compare_semver(&new_semver, &target_semver) == 0);
            } else {
                matched = (strcmp(new_app_info->version, target_version) == 0);
            }

            if (!matched) {
                ESP_LOGE(TAG, "Phien ban OTA %s khong khop target_version Firebase %s.",
                         new_app_info->version, target_version);
                if (reason_buf != NULL && reason_buf_len > 0) {
                    snprintf(reason_buf, reason_buf_len, "target_version_mismatch");
                }
                return ESP_FAIL;
            }
        } else if (ota_version_is_numeric(target_version)) {
            ESP_LOGW(TAG,
                     "target_version Firebase dang o dang timestamp (%s), bo qua kiem tra chat voi phien ban app %s.",
                     target_version, new_app_info->version);
        } else {
            ESP_LOGW(TAG,
                     "target_version Firebase khong phai semver (%s), bo qua kiem tra chat.",
                     target_version);
        }
    }

    return ESP_OK;
}

// CORE OTA FLOW
// Thực hiện xử lý trong ota_check_and_update.
bool ota_check_and_update(void) {
    char fw_url[MAX_URL_LEN] = {0};
    char target_version[64] = {0};
    char validation_reason[64] = {0};
    char ota_error[96] = {0};
    char ota_fw_url_path[OTA_STATION_PATH_MAX_LEN];
    char ota_version_path[OTA_STATION_PATH_MAX_LEN];
    char ota_cmd_fw_url_path[OTA_STATION_PATH_MAX_LEN];
    char ota_cmd_url_path[OTA_STATION_PATH_MAX_LEN];
    int ota_progress = 0;
    bool ota_started = false;

    ESP_LOGI(TAG, "Bat dau quy trinh OTA...");

    ota_firebase_load_config();

    if (!ota_firebase_have_config()) {
        ESP_LOGE(TAG, "Thieu cau hinh Firebase, khong the chay OTA.");
        ota_firebase_publish_state("error", 0, "missing_firebase_config");
        return false;
    }

    ota_firebase_build_station_path(ota_fw_url_path,     sizeof(ota_fw_url_path),     "/ota/firmware_url");
    ota_firebase_build_station_path(ota_version_path,    sizeof(ota_version_path),    "/ota/version");
    ota_firebase_build_station_path(ota_cmd_fw_url_path, sizeof(ota_cmd_fw_url_path), "/command/config/firmware_url");
    ota_firebase_build_station_path(ota_cmd_url_path,    sizeof(ota_cmd_url_path),    "/command/config/url");

    esp_err_t meta_err = ota_firebase_get_string(ota_fw_url_path, fw_url, sizeof(fw_url));
    if (meta_err != ESP_OK) {
        ESP_LOGW(TAG, "Khong doc duoc ota/firmware_url (%s), thu duong du phong command/config/firmware_url",
                 esp_err_to_name(meta_err));
        meta_err = ota_firebase_get_string(ota_cmd_fw_url_path, fw_url, sizeof(fw_url));
    }
    if (meta_err != ESP_OK) {
        ESP_LOGW(TAG, "Khong doc duoc command/config/firmware_url (%s), thu command/config/url",
                 esp_err_to_name(meta_err));
        meta_err = ota_firebase_get_string(ota_cmd_url_path, fw_url, sizeof(fw_url));
    }
    if (meta_err != ESP_OK) {
        ESP_LOGE(TAG, "Khong tai duoc firmware_url OTA tu Firebase: %s", esp_err_to_name(meta_err));
        const char *meta_error = "firmware_url_read_failed";
        if (meta_err == ESP_ERR_NOT_FOUND) {
            meta_error = "firmware_url_missing";
        } else if (meta_err == ESP_ERR_INVALID_RESPONSE) {
            meta_error = "firmware_url_invalid_response";
        }
        ota_firebase_publish_state("error", 0, meta_error);
        return false;
    }

    meta_err = ota_firebase_get_string(ota_version_path, target_version, sizeof(target_version));
    if (meta_err == ESP_ERR_NOT_FOUND) {
        target_version[0] = '\0';
        ESP_LOGW(TAG, "Firebase chua dat ota/version, tiep tuc chi kiem tra header firmware.");
    } else if (meta_err != ESP_OK) {
        target_version[0] = '\0';
        ESP_LOGW(TAG, "Khong doc duoc phien ban OTA tu Firebase: %s", esp_err_to_name(meta_err));
    }

    size_t fw_url_len = strnlen(fw_url, sizeof(fw_url));
    if (fw_url_len == 0 || fw_url_len >= (sizeof(fw_url) - 1)) {
        ESP_LOGE(TAG, "URL firmware OTA khong hop le hoac bi cat ngan.");
        ota_firebase_publish_state("error", 0, "invalid_firmware_url");
        return false;
    }
    if (strncmp(fw_url, "https://", 8) != 0 && strncmp(fw_url, "http://", 7) != 0) {
        ESP_LOGE(TAG, "URL firmware OTA phai bat dau bang http/https: %s", fw_url);
        ota_firebase_publish_state("error", 0, "invalid_firmware_url");
        return false;
    }

    ESP_LOGI(TAG, "URL firmware OTA: %s", fw_url);
    if (target_version[0] != '\0') {
        ESP_LOGI(TAG, "Phien ban muc tieu OTA: %s", target_version);
    }

    ota_firebase_publish_state("downloading", 0, NULL);

    beep(1, 1000);

    esp_http_client_config_t config = {
        .url = fw_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong ket noi duoc toi OTA server: %s", esp_err_to_name(err));
        snprintf(ota_error, sizeof(ota_error), "begin_%s", esp_err_to_name(err));
        ota_firebase_publish_state("error", 0, ota_error);
        beep(3, 200);
        return false;
    }
    ota_started = true;

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong doc duoc header OTA image: %s", esp_err_to_name(err));
        snprintf(ota_error, sizeof(ota_error), "read_image_header_%s", esp_err_to_name(err));
        goto ota_fail;
    }

    err = validate_image_header(&app_desc, target_version, validation_reason, sizeof(validation_reason));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Kiem tra OTA image that bai.");
        if (validation_reason[0] != '\0') {
            snprintf(ota_error, sizeof(ota_error), "%s", validation_reason);
        } else {
            snprintf(ota_error, sizeof(ota_error), "image_validation_failed");
        }
        goto ota_fail;
    }

    ota_progress = 35;
    ota_firebase_publish_state("flashing", ota_progress, NULL);

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int bytes_read = esp_https_ota_get_image_len_read(https_ota_handle);
        int next_progress = 40 + (bytes_read / 8192);
        if (next_progress > 90) {
            next_progress = 90;
        }
        if (next_progress > ota_progress) {
            ota_progress = next_progress;
            ota_firebase_publish_state("flashing", ota_progress, NULL);
        }

        ESP_LOGD(TAG, "Dang tai cac khoi OTA... %d bytes", bytes_read);
    }

    if (err == ESP_OK) {
        esp_err_t finish_err = esp_https_ota_finish(https_ota_handle);
        if (finish_err != ESP_OK) {
            ESP_LOGE(TAG, "Ket thuc OTA that bai: %s", esp_err_to_name(finish_err));
            snprintf(ota_error, sizeof(ota_error), "finish_%s", esp_err_to_name(finish_err));
            err = finish_err;
            goto ota_fail;
        }

        ota_firebase_publish_state("success", 100, NULL);
        ESP_LOGI(TAG, "OTA hoan tat thanh cong, dang khoi dong lai...");
        beep(3, 500);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return true;
    }

ota_fail:
    if (ota_started && https_ota_handle != NULL) {
        esp_https_ota_abort(https_ota_handle);
    }

    if (ota_error[0] == '\0') {
        snprintf(ota_error, sizeof(ota_error), "ota_%s", esp_err_to_name(err));
    }
    ota_firebase_publish_state("error", ota_progress, ota_error);
    ESP_LOGE(TAG, "OTA that bai: %s", esp_err_to_name(err));
    beep(8, 100);
    return false;
}

// TASK RUNNER
// Thực hiện xử lý trong ota_task_runner.
static void ota_task_runner(void *arg) {
    (void)arg;
    ota_check_and_update();
    ota_mark_idle();
    vTaskDelete(NULL);
}
// Gửi yêu cầu trong ota_request_start.
bool ota_request_start(void) {
    ota_firebase_load_config();

    taskENTER_CRITICAL(&s_ota_state_lock);
    const bool already_running = s_ota_in_progress;
    if (!already_running) {
        s_ota_in_progress = true;
    }
    taskEXIT_CRITICAL(&s_ota_state_lock);

    if (already_running) {
        ESP_LOGW(TAG, "Bo qua yeu cau OTA vi OTA dang chay.");
        return false;
    }

    BaseType_t task_ok = xTaskCreate(
        ota_task_runner,
        "ota_task",
        OTA_TASK_STACK_SIZE_BYTES,
        NULL,
        OTA_TASK_PRIORITY,
        &s_ota_task_handle
    );

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc tac vu OTA.");
        ota_firebase_publish_state("error", 0, "ota_task_create_failed");
        ota_mark_idle();
        return false;
    }

    return true;
}


