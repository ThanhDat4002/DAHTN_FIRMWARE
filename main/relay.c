#include "relay.h"
#include "config.h"
#include "sensor.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "RELAY"

static volatile bool      s_relay_state = false;
static SemaphoreHandle_t s_relay_mutex = NULL;
// Khởi tạo thành phần trong relay_init.
void relay_init(void) {
    if (s_relay_mutex == NULL) {
        s_relay_mutex = xSemaphoreCreateMutex();
        configASSERT(s_relay_mutex != NULL);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << RELAY_PIN),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_ENABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Ensure relay OFF at boot.
    gpio_set_level(RELAY_PIN, 0);
    s_relay_state = false;
    ESP_LOGI(TAG, "Relay da khoi tao - DANG TAT (GPIO %d)", RELAY_PIN);
}
// Thực hiện xử lý trong relay_on.
void relay_on(void) {
    if (s_relay_mutex != NULL) {
        xSemaphoreTake(s_relay_mutex, portMAX_DELAY);
    }
    gpio_set_level(RELAY_PIN, 1);
    s_relay_state = true;
    if (s_relay_mutex != NULL) {
        xSemaphoreGive(s_relay_mutex);
    }

    sensor_wakeup_now();
    ESP_LOGI(TAG, ">>> RELAY BAT - da cap dien sac <<<");
}
// Thực hiện xử lý trong relay_off.
void relay_off(void) {
    if (s_relay_mutex != NULL) {
        xSemaphoreTake(s_relay_mutex, portMAX_DELAY);
    }
    gpio_set_level(RELAY_PIN, 0);
    s_relay_state = false;
    if (s_relay_mutex != NULL) {
        xSemaphoreGive(s_relay_mutex);
    }

    ESP_LOGI(TAG, ">>> RELAY TAT - da ngat dien sac <<<");
}
// Thực hiện xử lý trong relay_is_on.
bool relay_is_on(void) {
    bool on = false;
    if (s_relay_mutex != NULL) {
        xSemaphoreTake(s_relay_mutex, portMAX_DELAY);
        on = s_relay_state;
        xSemaphoreGive(s_relay_mutex);
    } else {
        on = s_relay_state;
    }
    return on;
}


