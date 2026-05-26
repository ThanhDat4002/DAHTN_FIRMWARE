/**
 * @file nvs_manager.c
 * @brief Triá»ƒn khai lÆ°u/Ä‘á»c cáº¥u hÃ¬nh há»‡ thá»‘ng vÃ  phiÃªn sáº¡c qua NVS Flash.
 */

#include "nvs_manager.h"
#include "config.h"

#include <string.h>
#include <stdint.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define TAG "NVS"

// KHá»žI Táº O
// Khởi tạo thành phần trong nvs_manager_init.
void nvs_manager_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS Flash bi loi, dang xoa va format lai...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS Flash da khoi tao OK.");
}

// WIFI CONFIG
// Lưu dữ liệu trong nvs_save_wifi_config.
bool nvs_save_wifi_config(const wifi_config_data_t *config) {
    nvs_handle_t handle;
    esp_err_t err;

    // --- LÆ°u WiFi credentials ---
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo NVS namespace '%s': %s", NVS_NAMESPACE_WIFI, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, config->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_PASSWORD, config->password);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khi ghi cau hinh WiFi vao NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Da luu cau hinh WiFi cho SSID='%s'", config->ssid);
        return true;
    }

    ESP_LOGE(TAG, "Loi khi commit NVS: %s", esp_err_to_name(err));
    return false;
}
// Tải dữ liệu trong nvs_load_wifi_config.
bool nvs_load_wifi_config(wifi_config_data_t *config) {
    nvs_handle_t handle;
    esp_err_t err;
    size_t required_size;

    memset(config, 0, sizeof(wifi_config_data_t));

    // --- Äá»c WiFi credentials ---
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "Chua co namespace wifi_cfg trong NVS.");
        return false;
    }

    required_size = sizeof(config->ssid);
    if (nvs_get_str(handle, NVS_KEY_SSID, config->ssid, &required_size) != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    required_size = sizeof(config->password);
    nvs_get_str(handle, NVS_KEY_PASSWORD, config->password, &required_size);
    nvs_close(handle);

    ESP_LOGI(TAG, "Da doc cau hinh WiFi: SSID='%s'", config->ssid);
    return (strlen(config->ssid) > 0);
}
// Xóa dữ liệu trong nvs_clear_wifi_config.
void nvs_clear_wifi_config(void) {
    nvs_handle_t handle;

    if (nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Da xoa toan bo cau hinh WiFi khoi NVS.");
}

// CLOUD CONFIG
// Cập nhật giá trị trong set_if_nonempty_str.
static esp_err_t set_if_nonempty_str(nvs_handle_t handle, const char *key, const char *value) {
    if (value == NULL || value[0] == '\0') {
        // Non-destructive update: truong rong thi giu nguyen gia tri cu trong NVS.
        return ESP_OK;
    }
    return nvs_set_str(handle, key, value);
}
// Lưu dữ liệu trong nvs_save_cloud_config.
bool nvs_save_cloud_config(const wifi_config_data_t *config) {
    nvs_handle_t handle;
    esp_err_t err;

    if (config == NULL) {
        return false;
    }

    err = nvs_open(NVS_NAMESPACE_FIREBASE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo NVS namespace '%s': %s",
                 NVS_NAMESPACE_FIREBASE, esp_err_to_name(err));
        return false;
    }

    err = set_if_nonempty_str(handle, NVS_KEY_FB_URL, config->firebase_url);
    if (err == ESP_OK) {
        err = set_if_nonempty_str(handle, NVS_KEY_FB_TOKEN, config->firebase_token);
    }
    if (err == ESP_OK) {
        err = set_if_nonempty_str(handle, NVS_KEY_DEVICE_ID, config->device_id);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Da luu cloud config vao NVS (device_id='%s').",
                 (config->device_id[0] != '\0') ? config->device_id : "<empty>");
        return true;
    }

    ESP_LOGE(TAG, "Loi khi luu cloud config vao NVS: %s", esp_err_to_name(err));
    return false;
}
// Tải dữ liệu trong nvs_load_cloud_config.
bool nvs_load_cloud_config(wifi_config_data_t *config) {
    nvs_handle_t handle;
    esp_err_t err;
    bool has_any_field = false;
    size_t required_size;

    if (config == NULL) {
        return false;
    }

    err = nvs_open(NVS_NAMESPACE_FIREBASE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    required_size = sizeof(config->firebase_url);
    if (nvs_get_str(handle, NVS_KEY_FB_URL, config->firebase_url, &required_size) == ESP_OK &&
        config->firebase_url[0] != '\0') {
        has_any_field = true;
    }

    required_size = sizeof(config->firebase_token);
    if (nvs_get_str(handle, NVS_KEY_FB_TOKEN, config->firebase_token, &required_size) == ESP_OK &&
        config->firebase_token[0] != '\0') {
        has_any_field = true;
    }

    required_size = sizeof(config->device_id);
    if (nvs_get_str(handle, NVS_KEY_DEVICE_ID, config->device_id, &required_size) == ESP_OK &&
        config->device_id[0] != '\0') {
        has_any_field = true;
    }

    nvs_close(handle);
    return has_any_field;
}
// Lưu dữ liệu trong nvs_save_runtime_max_current_limit.
bool nvs_save_runtime_max_current_limit(float max_current_limit) {
    nvs_handle_t handle;
    esp_err_t err;
    int32_t max_current_ma;

    if (max_current_limit <= 0.0f) {
        return false;
    }

    max_current_ma = (int32_t)((max_current_limit * 1000.0f) + 0.5f);
    if (max_current_ma <= 0) {
        return false;
    }

    err = nvs_open(NVS_NAMESPACE_FIREBASE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo namespace '%s' de luu runtime max current: %s",
                 NVS_NAMESPACE_FIREBASE, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_i32(handle, NVS_KEY_RUNTIME_MAX_CUR, max_current_ma);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khi luu runtime max current vao NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Da luu runtime max current: %.3fA", max_current_limit);
    return true;
}
// Tải dữ liệu trong nvs_load_runtime_max_current_limit.
bool nvs_load_runtime_max_current_limit(float *out_limit) {
    nvs_handle_t handle;
    esp_err_t err;
    int32_t max_current_ma = 0;

    if (out_limit == NULL) {
        return false;
    }

    err = nvs_open(NVS_NAMESPACE_FIREBASE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_i32(handle, NVS_KEY_RUNTIME_MAX_CUR, &max_current_ma);
    nvs_close(handle);
    if (err != ESP_OK || max_current_ma <= 0) {
        return false;
    }

    *out_limit = (float)max_current_ma / 1000.0f;
    return true;
}

// FAULT LATCH
// Lưu dữ liệu trong nvs_save_fault_latch.
bool nvs_save_fault_latch(error_code_t error_code) {
    nvs_handle_t handle;
    esp_err_t err;

    if (error_code == ERR_NONE) {
        return false;
    }

    err = nvs_open(NVS_NAMESPACE_FAULT, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong the mo namespace '%s' de luu fault latch: %s",
                 NVS_NAMESPACE_FAULT, esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(handle, NVS_KEY_FAULT_LATCHED, 1);
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_FAULT_CODE, (int32_t)error_code);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khi luu fault latch vao NVS: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGW(TAG, "Da luu FAULT latch vao NVS (error_code=%d).", (int)error_code);
    return true;
}

// Tải dữ liệu trong nvs_load_fault_latch.
bool nvs_load_fault_latch(error_code_t *out_error_code) {
    nvs_handle_t handle;
    esp_err_t err;
    uint8_t latched = 0;
    int32_t code = (int32_t)ERR_NONE;

    if (out_error_code == NULL) {
        return false;
    }
    *out_error_code = ERR_NONE;

    err = nvs_open(NVS_NAMESPACE_FAULT, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_u8(handle, NVS_KEY_FAULT_LATCHED, &latched);
    if (err != ESP_OK || latched == 0) {
        nvs_close(handle);
        return false;
    }

    if (nvs_get_i32(handle, NVS_KEY_FAULT_CODE, &code) != ESP_OK) {
        code = (int32_t)ERR_SENSOR_ERROR;
    }
    nvs_close(handle);

    if (code < (int32_t)ERR_NONE || code > (int32_t)ERR_UNPLUGGED) {
        code = (int32_t)ERR_SENSOR_ERROR;
    }

    *out_error_code = (error_code_t)code;
    return true;
}

// Xóa dữ liệu trong nvs_clear_fault_latch.
void nvs_clear_fault_latch(void) {
    nvs_handle_t handle;

    if (nvs_open(NVS_NAMESPACE_FAULT, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Da xoa FAULT latch khoi NVS.");
    }
}

// SESSION RECOVERY
// Lưu dữ liệu trong nvs_save_session.
bool nvs_save_session(const charging_session_t *session) {
    nvs_handle_t handle;
    esp_err_t err;
    int32_t energy_mwh;
    int32_t max_current_ma;
    int32_t target_energy_mwh;

    err = nvs_open(NVS_NAMESPACE_SESSION, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong mo duoc namespace session: %s", esp_err_to_name(err));
        return false;
    }

    energy_mwh = (int32_t)(session->energy_used_kwh * 1000.0f);
    max_current_ma = (session->max_current_limit > 0.0f)
                         ? (int32_t)(session->max_current_limit * 1000.0f)
                         : 0;
    target_energy_mwh = (session->target_energy_kwh > 0.0f)
                            ? (int32_t)(session->target_energy_kwh * 1000.0f)
                            : 0;

    err = nvs_set_u8(handle, NVS_KEY_SESSION_ACTIVE, session->active ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_SESSION_ID, session->session_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, NVS_KEY_SESSION_USER, session->user_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_i64(handle, NVS_KEY_SESSION_START, session->start_time);
    }
    if (err == ESP_OK) {
        err = nvs_set_i64(handle, NVS_KEY_SESSION_END, session->end_time);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_SESSION_ENERGY, energy_mwh);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_KEY_SESSION_REASON, (uint8_t)session->end_reason);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_SESSION_MAX_CUR, max_current_ma);
    }
    if (err == ESP_OK) {
        err = nvs_set_i32(handle, NVS_KEY_SESSION_TARGET, target_energy_mwh);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi khi ghi session vao NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Da luu session '%s' (%.3f kWh) vao NVS.",
                 session->session_id, session->energy_used_kwh);
        return true;
    }
    return false;
}
// Tải dữ liệu trong nvs_load_session.
bool nvs_load_session(charging_session_t *session) {
    nvs_handle_t handle;
    esp_err_t err;
    bool has_active_session = false;

    memset(session, 0, sizeof(charging_session_t));

    err = nvs_open(NVS_NAMESPACE_SESSION, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t active = 0;
    if (nvs_get_u8(handle, NVS_KEY_SESSION_ACTIVE, &active) != ESP_OK) {
        active = 0;
    }
    session->active = (active != 0);

    size_t len = sizeof(session->session_id);
    nvs_get_str(handle, NVS_KEY_SESSION_ID, session->session_id, &len);

    len = sizeof(session->user_id);
    nvs_get_str(handle, NVS_KEY_SESSION_USER, session->user_id, &len);

    nvs_get_i64(handle, NVS_KEY_SESSION_START, &session->start_time);
    nvs_get_i64(handle, NVS_KEY_SESSION_END, &session->end_time);

    int32_t energy_mwh = 0;
    nvs_get_i32(handle, NVS_KEY_SESSION_ENERGY, &energy_mwh);
    session->energy_used_kwh = (float)energy_mwh / 1000.0f;

    uint8_t end_reason = STOP_REASON_NONE;
    nvs_get_u8(handle, NVS_KEY_SESSION_REASON, &end_reason);
    session->end_reason = (stop_reason_t)end_reason;

    int32_t max_current_ma = 0;
    nvs_get_i32(handle, NVS_KEY_SESSION_MAX_CUR, &max_current_ma);
    session->max_current_limit = (float)max_current_ma / 1000.0f;

    int32_t target_energy_mwh = 0;
    nvs_get_i32(handle, NVS_KEY_SESSION_TARGET, &target_energy_mwh);
    session->target_energy_kwh = (float)target_energy_mwh / 1000.0f;

    nvs_close(handle);

    has_active_session = session->active && (session->session_id[0] != '\0');
    if (!has_active_session) {
        return false;
    }

    ESP_LOGI(TAG, "Tim thay phien sac dang do trong NVS: '%s' (%.3f kWh da sac)",
             session->session_id, session->energy_used_kwh);
    return true;
}
// Xóa dữ liệu trong nvs_clear_session.
void nvs_clear_session(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE_SESSION, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGD(TAG, "Da xoa du lieu session khoi NVS.");
    }
}


