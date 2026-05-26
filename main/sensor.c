
#include "sensor.h"
#include "globals.h"
#include "config.h"
#include "relay.h"

#include <math.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "SENSOR"

// Modbus RTU constants
#define PZEM_REQ_LEN        8
#define PZEM_RESP_LEN       25
#define PZEM_RX_BUF_LEN     32
#define PZEM_FUNC_READ      0x04
#define PZEM_READ_TIMEOUT_MS 150
#define SENSOR_MIN_VOLTAGE_VALID_V 50.0f
#define SENSOR_MAX_VOLTAGE_VALID_V 300.0f
#define SENSOR_MAX_CURRENT_VALID_A 120.0f
#define SENSOR_MAX_POWER_VALID_W   30000.0f
#define SENSOR_INFO_LOG_PERIOD_MS 30000U

// So lan doc loi lien tiep de nang cap log SENSOR_ERROR
#define SENSOR_MAX_ERRORS   3
static uint64_t s_force_sample_until_ms = 0;
static uint8_t s_pzem_addr = PZEM_SLAVE_ADDR;
static TaskHandle_t s_sensor_task_handle = NULL;
static uint64_t s_last_len_error_log_ms = 0;
static int s_last_len_error_rx = -999;
static uint64_t s_last_crc_error_log_ms = 0;
static uint16_t s_last_crc_recv = 0;
static uint16_t s_last_crc_calc = 0;
static uint64_t s_last_outlier_log_ms = 0;
static float s_last_outlier_v = -1.0f;
static float s_last_outlier_i = -1.0f;
static float s_last_outlier_p = -1.0f;
// Thực hiện xử lý trong monotonic_ms.
static uint64_t monotonic_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}
// Thực hiện xử lý trong sensor_should_log_by_time_and_change.
static bool sensor_should_log_by_time_and_change(uint64_t *last_log_ms,
                                                 uint32_t min_interval_ms,
                                                 bool changed) {
    const uint64_t now = monotonic_ms();
    if (changed || (now - *last_log_ms) >= min_interval_ms) {
        *last_log_ms = now;
        return true;
    }
    return false;
}
// Thực hiện xử lý trong sensor_value_changed_significant.
static bool sensor_value_changed_significant(const pzem_data_t *a, const pzem_data_t *b) {
    if (!a->valid || !b->valid) {
        return true;
    }
    return (fabsf(a->voltage - b->voltage) >= 0.2f) ||
           (fabsf(a->current - b->current) >= 0.02f) ||
           (fabsf(a->power - b->power) >= 2.0f) ||
           (fabsf(a->energy_kwh - b->energy_kwh) >= 0.001f);
}
// Thực hiện xử lý trong modbus_crc16.
static uint16_t modbus_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc & 0x0001U) != 0U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
// Thực hiện xử lý trong sensor_should_sample.
static bool sensor_should_sample(void) {
    charging_session_t session;
    uint64_t now = monotonic_ms();
    globals_get_session_snapshot(&session);
    return relay_is_on() ||
           session.active ||
           (s_force_sample_until_ms != 0 && now < s_force_sample_until_ms);
}
// Gửi dữ liệu trong sensor_publish_idle_sample.
static void sensor_publish_idle_sample(void) {
    pzem_data_t idle_data = {0};
    idle_data.valid = false;
    idle_data.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    globals_update_sensor_data(&idle_data);
}
// Gửi dữ liệu trong sensor_publish_error_sample.
static void sensor_publish_error_sample(pzem_data_t *data) {
    memset(data, 0, sizeof(*data));
    data->valid = false;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    globals_update_sensor_data(data);
    xQueueOverwrite(g_sensor_queue, data);
}
// Khôi phục trạng thái trong sensor_recover_uart.
static void sensor_recover_uart(void) {
    ESP_LOGW(TAG, "Thu khoi dong lai UART PZEM...");
    (void)uart_flush(PZEM_UART_NUM);
    (void)uart_wait_tx_done(PZEM_UART_NUM, pdMS_TO_TICKS(100));
    (void)uart_driver_delete(PZEM_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(100));
    sensor_init();
}
// Đánh thức tác vụ trong sensor_wakeup_now.
void sensor_wakeup_now(void) {
    TaskHandle_t handle = s_sensor_task_handle;
    if (handle != NULL) {
        (void)xTaskAbortDelay(handle);
    }
}
// Khởi tạo thành phần trong sensor_init.
void sensor_init(void) {
    const uart_config_t uart_cfg = {
        .baud_rate  = PZEM_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(PZEM_UART_NUM,
                                        PZEM_BUF_SIZE * 2,
                                        PZEM_BUF_SIZE * 2,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PZEM_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_mode(PZEM_UART_NUM, UART_MODE_UART));
    ESP_ERROR_CHECK(uart_set_pin(PZEM_UART_NUM,
                                 PZEM_TXD_PIN,
                                 PZEM_RXD_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    s_pzem_addr = PZEM_SLAVE_ADDR;

    ESP_LOGI(TAG, "UART%d da khoi tao OK (TX=%d RX=%d %d baud)",
             PZEM_UART_NUM, PZEM_TXD_PIN, PZEM_RXD_PIN, PZEM_UART_BAUD);
}
// Thực hiện xử lý trong sensor_read_once.
bool sensor_read_once(pzem_data_t *data) {
    memset(data, 0, sizeof(*data));
    data->valid = false;

    uint8_t req[PZEM_REQ_LEN] = {
        s_pzem_addr, PZEM_FUNC_READ, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00
    };
    uint16_t crc = modbus_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)((crc >> 8) & 0xFF);

    uart_flush(PZEM_UART_NUM);
    const int sent = uart_write_bytes(PZEM_UART_NUM, (const char *)req, PZEM_REQ_LEN);
    if (sent != PZEM_REQ_LEN) {
        ESP_LOGW(TAG, "Gui yeu cau that bai (%d/%d bytes)", sent, PZEM_REQ_LEN);
        return false;
    }

    uint8_t rx_buf[PZEM_RX_BUF_LEN] = {0};
    int rx_len = uart_read_bytes(PZEM_UART_NUM,
                                 rx_buf,
                                 sizeof(rx_buf),
                                 pdMS_TO_TICKS(PZEM_READ_TIMEOUT_MS));
    if (rx_len != PZEM_RESP_LEN) {
        const bool changed = (rx_len != s_last_len_error_rx);
        if (sensor_should_log_by_time_and_change(&s_last_len_error_log_ms, 1500U, changed)) {
            ESP_LOGW(TAG, "Do dai khung Modbus sai: nhan %d bytes (mong doi %d)",
                     rx_len, PZEM_RESP_LEN);
            s_last_len_error_rx = rx_len;
        }
        return false;
    }

    const uint16_t crc_calc = modbus_crc16(rx_buf, (uint16_t)(rx_len - 2));
    const uint16_t crc_recv = ((uint16_t)rx_buf[rx_len - 1] << 8) | rx_buf[rx_len - 2];
    if (crc_calc != crc_recv) {
        const bool changed = (crc_recv != s_last_crc_recv) || (crc_calc != s_last_crc_calc);
        if (sensor_should_log_by_time_and_change(&s_last_crc_error_log_ms, 1500U, changed)) {
            ESP_LOGW(TAG, "CRC khong khop: nhan 0x%04X, tinh 0x%04X", crc_recv, crc_calc);
            s_last_crc_recv = crc_recv;
            s_last_crc_calc = crc_calc;
        }
        return false;
    }

    data->voltage = (float)(((uint16_t)rx_buf[3] << 8) | rx_buf[4]) / 10.0f;
    data->current = (float)(((uint32_t)rx_buf[7] << 24) | ((uint32_t)rx_buf[8] << 16) |
                            ((uint32_t)rx_buf[5] << 8)  | (uint32_t)rx_buf[6]) / 1000.0f;
    data->power = (float)(((uint32_t)rx_buf[11] << 24) | ((uint32_t)rx_buf[12] << 16) |
                          ((uint32_t)rx_buf[9] << 8)   | (uint32_t)rx_buf[10]) / 10.0f;
    data->energy_kwh = (float)(((uint32_t)rx_buf[15] << 24) | ((uint32_t)rx_buf[16] << 16) |
                               ((uint32_t)rx_buf[13] << 8)  | (uint32_t)rx_buf[14]) / 1000.0f;
    data->frequency = (float)(((uint16_t)rx_buf[17] << 8) | rx_buf[18]) / 10.0f;
    data->power_factor = (float)(((uint16_t)rx_buf[19] << 8) | rx_buf[20]) / 100.0f;

    if (data->voltage < SENSOR_MIN_VOLTAGE_VALID_V ||
        data->voltage > SENSOR_MAX_VOLTAGE_VALID_V ||
        data->current < 0.0f ||
        data->current > SENSOR_MAX_CURRENT_VALID_A ||
        data->power < 0.0f ||
        data->power > SENSOR_MAX_POWER_VALID_W) {
        const bool changed = (fabsf(data->voltage - s_last_outlier_v) >= 0.2f) ||
                             (fabsf(data->current - s_last_outlier_i) >= 0.02f) ||
                             (fabsf(data->power - s_last_outlier_p) >= 2.0f);
        if (sensor_should_log_by_time_and_change(&s_last_outlier_log_ms, 2000U, changed)) {
            ESP_LOGW(TAG,
                     "Bo qua mau bat thuong: V=%.1f I=%.3f P=%.1f",
                     data->voltage, data->current, data->power);
            s_last_outlier_v = data->voltage;
            s_last_outlier_i = data->current;
            s_last_outlier_p = data->power;
        }
        return false;
    }

    data->valid = true;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return true;
}
// Chạy tác vụ trong sensor_task.
void sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Tac vu cam bien bat dau (Core %d)", xPortGetCoreID());
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    s_sensor_task_handle = xTaskGetCurrentTaskHandle();

    pzem_data_t data;
    int error_count = 0;
    TickType_t last_wake = xTaskGetTickCount();
    bool was_sampling = false;
    uint64_t last_pzem_info_log_ms = 0;
    pzem_data_t last_pzem_info_sample = {0};
    bool last_relay_on = relay_is_on();
    uint64_t relay_on_since_ms = last_relay_on ? monotonic_ms() : 0;
    bool relay_on_start_logged = false;

    while (1) {
        const uint64_t now_ms = monotonic_ms();
        const bool relay_now = relay_is_on();
        if (!relay_now && last_relay_on) {
            s_force_sample_until_ms = now_ms + SENSOR_POST_RELAY_OFF_SAMPLE_MS;
            relay_on_since_ms = 0;
            relay_on_start_logged = false;
        } else if (relay_now) {
            s_force_sample_until_ms = 0;
            if (!last_relay_on) {
                relay_on_since_ms = now_ms;
                relay_on_start_logged = false;
            }
        }
        last_relay_on = relay_now;

        if (!sensor_should_sample()) {
            if (was_sampling) {
                sensor_publish_idle_sample();
            }
            was_sampling = false;
            error_count = 0;
            esp_task_wdt_reset();
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
            continue;
        }

        was_sampling = true;
        const bool in_relay_warmup =
            relay_now && relay_on_since_ms > 0 &&
            (now_ms - relay_on_since_ms) < SENSOR_BOOT_GRACE_MS;

        const bool ok = sensor_read_once(&data);

        if (ok) {
            error_count = 0;
            globals_update_sensor_data(&data);
            xQueueOverwrite(g_sensor_queue, &data);

            if (relay_now && !relay_on_start_logged && relay_on_since_ms > 0) {
                const uint64_t elapsed_ms = now_ms - relay_on_since_ms;
                ESP_LOGI(TAG,
                         "PZEM sau bat relay (%llums): V=%.1fV I=%.3fA P=%.1fW kWh=%.3f Hz=%.1f PF=%.2f",
                         elapsed_ms,
                         data.voltage, data.current, data.power,
                         data.energy_kwh, data.frequency, data.power_factor);
                relay_on_start_logged = true;
            }

            const bool changed = sensor_value_changed_significant(&data, &last_pzem_info_sample);
            const bool periodic = (now_ms - last_pzem_info_log_ms) >= SENSOR_INFO_LOG_PERIOD_MS;
            if (changed || periodic) {
                ESP_LOGI(TAG, "PZEM: V=%.2fV I=%.2fA P=%.2fW kWh=%.3f",
                         data.voltage, data.current, data.power, data.energy_kwh);
                last_pzem_info_log_ms = now_ms;
                last_pzem_info_sample = data;
            }

            ESP_LOGD(TAG, "V=%.1fV A=%.3fA P=%.1fW kWh=%.3f Hz=%.1f",
                     data.voltage, data.current, data.power, data.energy_kwh, data.frequency);
        } else {
            if (in_relay_warmup) {
                sensor_publish_error_sample(&data);
                esp_task_wdt_reset();
                vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
                continue;
            }

            if (error_count < SENSOR_MAX_ERRORS) {
                error_count++;
            }
            ESP_LOGW(TAG, "Doc PZEM that bai (lan %d/%d)", error_count, SENSOR_MAX_ERRORS);
            sensor_publish_error_sample(&data);

            if (error_count >= SENSOR_MAX_ERRORS) {
                ESP_LOGE(TAG, "LOI_CAM_BIEN: Mat ket noi PZEM-004T!");
                sensor_recover_uart();
                error_count = 0;
            }
        }

        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}


