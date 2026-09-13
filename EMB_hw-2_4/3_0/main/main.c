#include 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config.h"
#include "sma_filter.h"
#include "hardware.h"

static const char *TAG = "MAIN_APP";

void app_main(void)
{
    adc_oneshot_unit_handle_t adc_handle = NULL;
    sma_filter_t sma_filter;
    uint8_t led_state = 0;

    sma_init(&sma_filter);

    if (hardware_init(&adc_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Помилка ініціалізації периферії!");
        return;
    }

    ESP_LOGI(TAG, "Систему успішно запущено. Моніторинг освітленості...");

    while (1) {
        int raw_val = hardware_adc_read(adc_handle);
        uint32_t filtered_val = sma_update(&sma_filter, (uint32_t)raw_val);

        if (!led_state && filtered_val > THRESHOLD_DARK) {
            led_state = 1;
            hardware_led_set(led_state);
            ESP_LOGW(TAG, "[ПОДІЯ] УВІМКНЕНО LED | Фільтроване: %lu (Raw: %d)", filtered_val, raw_val);
        } 
        else if (led_state && filtered_val < THRESHOLD_LIGHT) {
            led_state = 0;
            hardware_led_set(led_state);
            ESP_LOGI(TAG, "[ПОДІЯ] ВИМКНЕНО LED | Фільтроване: %lu (Raw: %d)", filtered_val, raw_val);
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}