
#include "cloud_internal.h"
#include "config.h"
#include "wifi.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "CLOUD_HTTP"

typedef struct {
    char   *buf;
    size_t  used;
    size_t  cap;
} http_resp_buffer_t;

typedef struct {
    char url[MAX_URL_LEN + 128];
    esp_http_client_method_t method;
    const char *post_data;
    size_t post_len;
    char *resp_buf;
    size_t resp_len;
    const cloud_http_header_t *headers;
    size_t header_count;
} cloud_http_request_t;

static SemaphoreHandle_t s_firebase_mutex = NULL;
// Xử lý sự kiện trong http_event_handler.
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(evt->client) &&
        evt->user_data != NULL) {

        http_resp_buffer_t *resp = (http_resp_buffer_t *)evt->user_data;
        if (resp->buf == NULL || resp->cap == 0) {
            return ESP_OK;
        }

        size_t remaining = resp->cap - resp->used - 1;
        size_t to_copy = (evt->data_len < remaining) ? (size_t)evt->data_len : remaining;
        if (to_copy > 0) {
            memcpy(resp->buf + resp->used, evt->data, to_copy);
            resp->used += to_copy;
            resp->buf[resp->used] = '\0';
        }
    }
    return ESP_OK;
}
// Thực hiện xử lý trong perform_http_request.
static esp_err_t perform_http_request(const cloud_http_request_t *request) {
    http_resp_buffer_t resp_ctx = {
        .buf = request->resp_buf,
        .used = 0,
        .cap = request->resp_len,
    };

    esp_http_client_config_t config = {
        .url = request->url,
        .event_handler = http_event_handler,
        .user_data = (request->resp_buf != NULL) ? &resp_ctx : NULL,
        .timeout_ms = FIREBASE_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, request->method);
    if (request->headers != NULL && request->header_count > 0U) {
        for (size_t i = 0; i < request->header_count; ++i) {
            const cloud_http_header_t *header = &request->headers[i];
            if (header->key != NULL && header->value != NULL &&
                header->key[0] != '\0' && header->value[0] != '\0') {
                esp_http_client_set_header(client, header->key, header->value);
            }
        }
    }

    if (request->post_data != NULL && request->post_len > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, request->post_data, request->post_len);
    }

    if (request->resp_buf != NULL && request->resp_len > 0) {
        request->resp_buf[0] = '\0';
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            ESP_LOGW(TAG, "Trang thai HTTP: %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Loi yeu cau HTTP: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

// PUBLIC API
// Khởi tạo thành phần trong cloud_http_init.
void cloud_http_init(void) {
    if (s_firebase_mutex == NULL) {
        s_firebase_mutex = xSemaphoreCreateMutex();
        configASSERT(s_firebase_mutex != NULL);
    }
}
// Thực hiện xử lý trong cloud_http_request.
esp_err_t cloud_http_request(const char *url,
                             esp_http_client_method_t method,
                             const char *post_data,
                             char *resp_buf,
                             size_t resp_len,
                             const cloud_http_header_t *headers,
                             size_t header_count) {
    bool have_mutex = false;
    uint32_t waited_ms = 0;
    uint32_t attempt = 0;
    const uint32_t max_attempts = (method == HTTP_METHOD_POST) ? 1U : CLOUD_HTTP_RETRY_COUNT;
    cloud_http_request_t request = {0};
    esp_err_t err = ESP_FAIL;

    if (s_firebase_mutex == NULL) {
        ESP_LOGW(TAG, "Firebase mutex chua san sang.");
        return ESP_FAIL;
    }

    while (waited_ms < CLOUD_HTTP_MUTEX_TIMEOUT_MS) {
        if (xSemaphoreTake(s_firebase_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            have_mutex = true;
            break;
        }
        cloud_task_wdt_reset_if_subscribed();
        waited_ms += 100;
    }

    if (!have_mutex) {
        ESP_LOGW(TAG, "HTTP mutex timeout sau %lu ms, bo qua yeu cau %s",
                 (unsigned long)CLOUD_HTTP_MUTEX_TIMEOUT_MS, url);
        return ESP_ERR_TIMEOUT;
    }

    if (strlen(url) >= sizeof(request.url)) {
        xSemaphoreGive(s_firebase_mutex);
        ESP_LOGW(TAG, "URL yeu cau qua dai, bo qua.");
        return ESP_ERR_INVALID_SIZE;
    }

    strncpy(request.url, url, sizeof(request.url) - 1);
    request.method = method;
    request.resp_buf = resp_buf;
    request.resp_len = resp_len;
    request.headers = headers;
    request.header_count = header_count;

    if (post_data != NULL) {
        size_t post_len = strlen(post_data);
        if (post_len >= (MAX_JSON_BUF_SIZE * 2U)) {
            xSemaphoreGive(s_firebase_mutex);
            ESP_LOGW(TAG, "Du lieu yeu cau qua dai, bo qua.");
            return ESP_ERR_INVALID_SIZE;
        }
        request.post_data = post_data;
        request.post_len = post_len;
    }

    while (attempt < max_attempts) {
        err = perform_http_request(&request);
        if (err == ESP_OK) {
            break;
        }

        attempt++;
        if (attempt < max_attempts && wifi_is_connected()) {
            ESP_LOGW(TAG, "Yeu cau HTTP that bai, thu lai lan %lu/%lu sau %lu ms.",
                     (unsigned long)(attempt + 1U),
                     (unsigned long)max_attempts,
                     (unsigned long)CLOUD_HTTP_RETRY_DELAY_MS);
            cloud_task_delay_with_wdt(CLOUD_HTTP_RETRY_DELAY_MS);
        }
    }

    xSemaphoreGive(s_firebase_mutex);
    return err;
}
// Thực hiện xử lý trong cloud_http_firebase_get.
esp_err_t cloud_http_firebase_get(const char *path, char *resp_buf, size_t resp_len) {
    char url[MAX_URL_LEN + 128];
    cloud_config_build_url(url, sizeof(url), path);
    return cloud_http_request(url, HTTP_METHOD_GET, NULL, resp_buf, resp_len, NULL, 0U);
}
// Thực hiện xử lý trong cloud_http_firebase_patch.
esp_err_t cloud_http_firebase_patch(const char *path, const char *json_payload) {
    char url[MAX_URL_LEN + 128];
    cloud_config_build_url(url, sizeof(url), path);
    return cloud_http_request(url, HTTP_METHOD_PATCH, json_payload, NULL, 0U, NULL, 0U);
}
// Thực hiện xử lý trong cloud_http_firebase_put_json.
esp_err_t cloud_http_firebase_put_json(const char *path, const char *json_payload) {
    char url[MAX_URL_LEN + 128];
    cloud_config_build_url(url, sizeof(url), path);
    return cloud_http_request(url, HTTP_METHOD_PUT, json_payload, NULL, 0U, NULL, 0U);
}
// Thực hiện xử lý trong cloud_http_firebase_put_string.
esp_err_t cloud_http_firebase_put_string(const char *path, const char *value) {
    cJSON *root = cJSON_CreateString(value);
    char *json = cJSON_PrintUnformatted(root);
    esp_err_t err = ESP_FAIL;

    if (json != NULL) {
        err = cloud_http_firebase_put_json(path, json);
        free(json);
    }
    cJSON_Delete(root);
    return err;
}


