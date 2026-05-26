/**
 * @file main.c
 * @brief Entry point cá»§a á»¨ng dá»¥ng EVION Firmware.
 *
 * Chá»‹u trÃ¡ch nhiá»‡m khá»Ÿi táº¡o cÃ¡c block pháº§n cá»©ng, phÃ¢n bá»• vÃ  ghim tÃ¡c vá»¥ (task pinning)
 * vÃ o 2 nhÃ¢n (Core 0 vÃ  Core 1) cá»§a ESP32.
 */

#include "config.h"
#include "globals.h"
#include "nvs_manager.h"
#include "relay.h"
#include "ui.h"
#include "sensor.h"
#include "wifi.h"
#include "safety.h"
#include "cloud.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_task_wdt.h" // Watchdog
#include "esp_timer.h" 

#define TAG "MAIN"

void app_main(void) {
    ESP_LOGI(TAG, "==== EVION FIRMWARE %s BAT DAU KHOI DONG ====", FIRMWARE_VERSION);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

  
    nvs_manager_init();

    
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,   // Bitmask cho cáº£ 2 core
        .trigger_panic = true,
    };
    esp_err_t wdt_status = esp_task_wdt_status(NULL);
    esp_err_t wdt_err = ESP_OK;

    if (wdt_status == ESP_ERR_INVALID_STATE) {
        wdt_err = esp_task_wdt_init(&wdt_config);
    } else {
        wdt_err = esp_task_wdt_reconfigure(&wdt_config);
    }

    if (wdt_err != ESP_OK) {
        ESP_ERROR_CHECK(wdt_err);
    }

    globals_init();

    error_code_t latched_fault_code = ERR_NONE;
    const bool fault_latched = nvs_load_fault_latch(&latched_fault_code);
    if (fault_latched) {
        globals_set_error_code(latched_fault_code);
        globals_set_system_state(STATE_FAULT);
        ESP_LOGE(TAG, "Phat hien FAULT latch trong NVS (error=%s). Can reset de mo khoa.",
                 error_code_to_str(latched_fault_code));
    }

    float runtime_max_current = 0.0f;
    if (nvs_load_runtime_max_current_limit(&runtime_max_current)) {
        const float loaded_max_current = runtime_max_current;
        if (runtime_max_current < REMOTE_MAX_CURRENT_MIN_A) {
            runtime_max_current = REMOTE_MAX_CURRENT_MIN_A;
        } else if (runtime_max_current > REMOTE_MAX_CURRENT_MAX_A) {
            runtime_max_current = REMOTE_MAX_CURRENT_MAX_A;
        }

        if (runtime_max_current != loaded_max_current) {
            if (!nvs_save_runtime_max_current_limit(runtime_max_current)) {
                ESP_LOGW(TAG, "Khong the cap nhat lai maxCurrent da clamp vao NVS.");
            }
        }

        globals_set_max_current_limit(runtime_max_current);
        ESP_LOGI(TAG, "Khoi phuc maxCurrent runtime tu NVS: %.3fA", runtime_max_current);
    }

    relay_init();
    if (fault_latched) {
        relay_off();
    }
    ui_init();      // LED/Buzzer
    sensor_init();  // UART cho PZEM-004T

   
    charging_session_t saved_session;
    if (nvs_load_session(&saved_session)) {
        ESP_LOGW(TAG,
                 "Phat hien session dang do sau reboot (co the mat dien). Khong resume, xoa session NVS: %s",
                 saved_session.session_id);
        relay_off();
        nvs_clear_session();
        if (!fault_latched) {
            globals_set_error_code(ERR_NONE);
        }
    }

    wifi_init();
    cloud_init();

    if (g_system_state != STATE_FAULT) {
        globals_set_system_state((globals_get_network_status() == NET_ONLINE) ? STATE_READY : STATE_IDLE);
    }

 
    
    // Core 1 (Hardware
    if (xTaskCreatePinnedToCore(safety_task, "safety_task", SAFETY_TASK_STACK, NULL, PRIORITY_SAFETY, NULL, CORE_HARDWARE) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc safety_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreatePinnedToCore(sensor_task, "sensor_task", SENSOR_TASK_STACK, NULL, PRIORITY_SENSOR, NULL, CORE_HARDWARE) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc sensor_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreatePinnedToCore(ui_task, "ui_task", UI_TASK_STACK, NULL, PRIORITY_UI, NULL, CORE_HARDWARE) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc ui_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    // Core 0 (Network, Cloud, Timeout)
    if (xTaskCreatePinnedToCore(cloud_publish_task, "cloud_pub", CLOUD_PUBLISH_STACK, NULL, PRIORITY_CLOUD, NULL, CORE_NETWORK) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc cloud_publish_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreatePinnedToCore(cloud_poll_task, "cloud_poll", CLOUD_POLL_STACK, NULL, PRIORITY_CLOUD, NULL, CORE_NETWORK) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc cloud_poll_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreatePinnedToCore(wifi_reset_button_task, "wifi_button", BUTTON_TASK_STACK, NULL, PRIORITY_BUTTON, NULL, CORE_NETWORK) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc wifi_reset_button_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    if (xTaskCreatePinnedToCore(network_task, "network", NETWORK_TASK_STACK, NULL, PRIORITY_NETWORK, NULL, CORE_NETWORK) != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc network_task.");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    
 
    uint64_t boot_time = esp_timer_get_time() / 1000000;
    bool app_marked_valid = false;
    
    while (1) {
        // Cáº­p nháº­t Update Time cho há»‡ thá»‘ng theo má»—i giÃ¢y
        g_uptime_seconds = (esp_timer_get_time() / 1000000) - boot_time;
#if CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
        if (!app_marked_valid &&
            g_uptime_seconds >= 10 &&
            globals_get_network_status() == NET_ONLINE &&
            globals_get_error_code() == ERR_NONE) {
            esp_err_t rollback_err = esp_ota_mark_app_valid_cancel_rollback();
            if (rollback_err == ESP_OK) {
                ESP_LOGI(TAG, "Khoi dong da on dinh, danh dau app hop le cho rollback.");
            } else {
                ESP_LOGW(TAG, "Khong the danh dau app hop le: %s", esp_err_to_name(rollback_err));
            }
            app_marked_valid = true;
        }
#endif
        esp_task_wdt_reset();
        // Watchdog nháº£ cho Task ChÃ­nh hoáº¡t Ä‘á»™ng bÃ¬nh thÆ°á»ng
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



