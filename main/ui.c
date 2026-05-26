#include "ui.h"
#include "globals.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#define TAG "UI"

typedef struct {
    int count;
    int duration_ms;
} beep_request_t;

static QueueHandle_t s_beep_queue = NULL;
// Tắt tất cả LED.
static inline void led_all_off(void) {
    gpio_set_level(LED_STATUS_PIN,   0);
    gpio_set_level(LED_CHARGING_PIN, 0);
    gpio_set_level(LED_FAULT_PIN,    0);
}
// Thực hiện xử lý trong play_beep_pattern.
static void play_beep_pattern(const beep_request_t *req) {
    if (req == NULL) {
        return;
    }

    const int count = (req->count > 0) ? req->count : 1;
    const int duration_ms = (req->duration_ms > 0) ? req->duration_ms : 100;

    for (int i = 0; i < count; i++) {
        gpio_set_level(BUZZER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        gpio_set_level(BUZZER_PIN, 0);
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        esp_task_wdt_reset();
    }
}
// Khởi tạo thành phần trong ui_init.
void ui_init(void) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_STATUS_PIN)  |
                        (1ULL << LED_CHARGING_PIN)|
                        (1ULL << LED_FAULT_PIN)   |
                        (1ULL << BUZZER_PIN),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    led_all_off();
    gpio_set_level(BUZZER_PIN, 0);

    if (s_beep_queue == NULL) {
        s_beep_queue = xQueueCreate(4, sizeof(beep_request_t));
        configASSERT(s_beep_queue != NULL);
    }

    ESP_LOGI(TAG, "UI (LED+Buzzer) da khoi tao.");
}
// Thực hiện xử lý trong beep.
void beep(int count, int duration_ms) {
    beep_request_t req = {
        .count = count,
        .duration_ms = duration_ms,
    };

    if (s_beep_queue == NULL) {
        play_beep_pattern(&req);
        return;
    }

    if (xQueueSend(s_beep_queue, &req, 0) != pdTRUE) {
        beep_request_t dropped;
        (void)xQueueReceive(s_beep_queue, &dropped, 0);
        (void)xQueueSend(s_beep_queue, &req, 0);
    }
}
// Chạy tác vụ trong ui_task.
void ui_task(void *pvParameters) {
    (void)pvParameters;

    ESP_LOGI(TAG, "Tac vu UI bat dau tren core %d", xPortGetCoreID());
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    system_state_t last_state = STATE_BOOT;

    while (1) {
        system_state_t state = globals_get_system_state();
        beep_request_t pending_beep;

        if (s_beep_queue != NULL &&
            xQueueReceive(s_beep_queue, &pending_beep, 0) == pdTRUE) {
            play_beep_pattern(&pending_beep);
        }

        if (state != last_state) {
            led_all_off();
            last_state = state;
        }

        switch (state) {
            case STATE_BOOT:
                gpio_set_level(LED_STATUS_PIN,   1);
                gpio_set_level(LED_CHARGING_PIN, 1);
                gpio_set_level(LED_FAULT_PIN,    1);
                vTaskDelay(pdMS_TO_TICKS(500));
                led_all_off();
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case STATE_IDLE:
            case STATE_OFFLINE:
                gpio_set_level(LED_STATUS_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(LED_STATUS_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;

            case STATE_READY:
            case STATE_STOPPED:
                gpio_set_level(LED_STATUS_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case STATE_CHARGING:
                gpio_set_level(LED_CHARGING_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(LED_CHARGING_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                break;

            case STATE_FAULT:
                gpio_set_level(LED_FAULT_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_FAULT_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_FAULT_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(LED_FAULT_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(1200));
                break;

            default:
                led_all_off();
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
        }

        esp_task_wdt_reset();
    }
}


