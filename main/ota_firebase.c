/**
 * @file ota_firebase.c
 * @brief Truy cáº­p Firebase cho OTA: load config, build URL, GET/PATCH metadata.
 */

#include "ota_firebase.h"
#include "config.h"
#include "nvs_manager.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "OTA_FIREBASE"
#define OTA_PROGRESS_UPDATE_STEP   5
#define OTA_STATUS_MIN_PUBLISH_US  1500000LL

typedef struct {
    char   *buf;
    size_t  used;
    size_t  cap;
} ota_http_buffer_t;

static wifi_config_data_t s_ota_cfg;
static bool s_ota_cfg_ready = false;

static char s_metadata_url_buf[MAX_URL_LEN + 128];
static char s_metadata_resp_buf[MAX_HTTP_RESPONSE_SIZE];

static char    s_last_ota_status[16]   = "";
static int     s_last_ota_progress     = -1;
static char    s_last_ota_error[96]    = "";
static int64_t s_last_ota_publish_us   = 0;

// HTTP EVENT HANDLER
// Xử lý sự kiện trong ota_http_event_handler.
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(evt->client) &&
        evt->user_data != NULL) {
        ota_http_buffer_t *resp = (ota_http_buffer_t *)evt->user_data;

        if (resp->buf == NULL || resp->cap == 0) {
            return ESP_OK;
        }

        size_t remaining = resp->cap - resp->used - 1;
        size_t to_copy = ((size_t)evt->data_len < remaining) ? (size_t)evt->data_len : remaining;
        if (to_copy > 0) {
            memcpy(resp->buf + resp->used, evt->data, to_copy);
            resp->used += to_copy;
            resp->buf[resp->used] = '\0';
        }
    }
    return ESP_OK;
}

// CONFIG LOADING
// Tải dữ liệu trong ota_firebase_load_config.
void ota_firebase_load_config(void) {
    wifi_config_data_t cfg_from_nvs = {0};

    memset(&s_ota_cfg, 0, sizeof(s_ota_cfg));
    strncpy(s_ota_cfg.firebase_url, FIREBASE_DATABASE_URL, sizeof(s_ota_cfg.firebase_url) - 1);
    strncpy(s_ota_cfg.firebase_token, FIREBASE_AUTH_TOKEN, sizeof(s_ota_cfg.firebase_token) - 1);
    strncpy(s_ota_cfg.device_id, FIREBASE_DEVICE_ID, sizeof(s_ota_cfg.device_id) - 1);

    if (nvs_load_cloud_config(&cfg_from_nvs)) {
        if (cfg_from_nvs.firebase_url[0] != '\0') {
            strncpy(s_ota_cfg.firebase_url, cfg_from_nvs.firebase_url,
                    sizeof(s_ota_cfg.firebase_url) - 1);
            s_ota_cfg.firebase_url[sizeof(s_ota_cfg.firebase_url) - 1] = '\0';
        }
        if (cfg_from_nvs.firebase_token[0] != '\0') {
            strncpy(s_ota_cfg.firebase_token, cfg_from_nvs.firebase_token,
                    sizeof(s_ota_cfg.firebase_token) - 1);
            s_ota_cfg.firebase_token[sizeof(s_ota_cfg.firebase_token) - 1] = '\0';
        }
        if (cfg_from_nvs.device_id[0] != '\0') {
            strncpy(s_ota_cfg.device_id, cfg_from_nvs.device_id,
                    sizeof(s_ota_cfg.device_id) - 1);
            s_ota_cfg.device_id[sizeof(s_ota_cfg.device_id) - 1] = '\0';
        }
    }

    size_t url_len = strlen(s_ota_cfg.firebase_url);
    if (url_len > 0 && s_ota_cfg.firebase_url[url_len - 1] == '/') {
        s_ota_cfg.firebase_url[url_len - 1] = '\0';
    }

    s_ota_cfg_ready = true;
}
// Thực hiện xử lý trong ota_firebase_have_config.
bool ota_firebase_have_config(void) {
    return s_ota_cfg_ready &&
           strlen(s_ota_cfg.firebase_url) > 0 &&
           strlen(s_ota_cfg.device_id) > 0;
}
// Tạo nội dung trong ota_firebase_build_station_path.
void ota_firebase_build_station_path(char *out_path, size_t out_len, const char *suffix) {
    const char *safe_suffix = (suffix != NULL) ? suffix : "";
    snprintf(out_path, out_len, "/stations/%s%s", s_ota_cfg.device_id, safe_suffix);
}

// URL BUILDER (Ná»˜I Bá»˜)
// Tạo nội dung trong build_firebase_metadata_url.
static void build_firebase_metadata_url(char *out_url, size_t out_len, const char *path) {
    if (!s_ota_cfg_ready) {
        ota_firebase_load_config();
    }

    if (strlen(s_ota_cfg.firebase_token) > 0) {
        snprintf(out_url, out_len, "%s%s.json?auth=%s",
                 s_ota_cfg.firebase_url, path, s_ota_cfg.firebase_token);
    } else {
        snprintf(out_url, out_len, "%s%s.json", s_ota_cfg.firebase_url, path);
    }
}

// HTTP PATCH / GET
// Thực hiện xử lý trong firebase_patch_json_metadata.
static esp_err_t firebase_patch_json_metadata(const char *path, const char *json_payload) {
    char url[MAX_URL_LEN + 128];
    build_firebase_metadata_url(url, sizeof(url), path);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = ota_http_event_handler,
        .timeout_ms = FIREBASE_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, (int)strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        const int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            err = ESP_FAIL;
        }
    }

    esp_http_client_cleanup(client);
    return err;
}
// Lấy dữ liệu trong ota_firebase_get_string.
esp_err_t ota_firebase_get_string(const char *path, char *out, size_t out_len) {
    ota_http_buffer_t resp_ctx = {
        .buf = s_metadata_resp_buf,
        .used = 0,
        .cap = sizeof(s_metadata_resp_buf),
    };

    if (out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    s_metadata_resp_buf[0] = '\0';
    build_firebase_metadata_url(s_metadata_url_buf, sizeof(s_metadata_url_buf), path);

    esp_http_client_config_t config = {
        .url = s_metadata_url_buf,
        .event_handler = ota_http_event_handler,
        .user_data = &resp_ctx,
        .timeout_ms = FIREBASE_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            ESP_LOGW(TAG, "Trang thai HTTP metadata Firebase %d tai %s", status, path);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Doc metadata (GET) that bai tai %s: %s", path, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return err;
    }

    if (s_metadata_resp_buf[0] == '\0' || strcmp(s_metadata_resp_buf, "null") == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *root = cJSON_Parse(s_metadata_resp_buf);
    if (!cJSON_IsString(root) || root->valuestring == NULL || root->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    strncpy(out, root->valuestring, out_len - 1);
    out[out_len - 1] = '\0';
    cJSON_Delete(root);
    return ESP_OK;
}

// PUBLISH STATE
// Gửi dữ liệu trong ota_firebase_publish_state.
void ota_firebase_publish_state(const char *status, int progress, const char *error_text) {
    const char *safe_status = (status != NULL) ? status : "ready";
    const char *safe_error  = (error_text != NULL) ? error_text : "";
    const int64_t now_us    = esp_timer_get_time();

    const bool same_status = (strcmp(s_last_ota_status, safe_status) == 0);
    const bool same_error  = (strcmp(s_last_ota_error, safe_error) == 0);
    const bool small_progress_step =
        (s_last_ota_progress >= 0 && progress >= 0 &&
         (progress - s_last_ota_progress) < OTA_PROGRESS_UPDATE_STEP);
    const bool within_min_interval =
        (s_last_ota_publish_us > 0 && (now_us - s_last_ota_publish_us) < OTA_STATUS_MIN_PUBLISH_US);
    const bool terminal_status =
        (strcmp(safe_status, "success") == 0 || strcmp(safe_status, "error") == 0);

    if (same_status && same_error && progress == s_last_ota_progress) {
        return;
    }

    if (!terminal_status && same_status && same_error && small_progress_step && within_min_interval) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "status", safe_status);
    cJSON_AddNumberToObject(root, "progress", progress);
    if (safe_error[0] != '\0') {
        cJSON_AddStringToObject(root, "error", safe_error);
    } else {
        cJSON_AddNullToObject(root, "error");
    }
    cJSON_AddNumberToObject(root, "updated_at", (double)(now_us / 1000000LL));

    char *json_payload = cJSON_PrintUnformatted(root);
    if (json_payload != NULL) {
        char ota_node_path[OTA_STATION_PATH_MAX_LEN];
        ota_firebase_build_station_path(ota_node_path, sizeof(ota_node_path), "/ota");
        const esp_err_t patch_err = firebase_patch_json_metadata(ota_node_path, json_payload);
        if (patch_err != ESP_OK) {
            ESP_LOGW(TAG, "Khong ghi duoc trang thai OTA len Firebase: %s",
                     esp_err_to_name(patch_err));
        }
        free(json_payload);
    }
    cJSON_Delete(root);

    strncpy(s_last_ota_status, safe_status, sizeof(s_last_ota_status) - 1);
    s_last_ota_status[sizeof(s_last_ota_status) - 1] = '\0';
    strncpy(s_last_ota_error, safe_error, sizeof(s_last_ota_error) - 1);
    s_last_ota_error[sizeof(s_last_ota_error) - 1] = '\0';
    s_last_ota_progress = progress;
    s_last_ota_publish_us = now_us;
}


