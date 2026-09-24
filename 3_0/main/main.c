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

    ESP_LOGI(TAG, "Система запущена. Моніторинг освітленості...");

    while (1) {
        int raw_val = 0;
        if (hardware_adc_read(adc_handle, &raw_val) == ESP_OK) {
            // Фільтрація значень
            uint32_t filtered_val = sma_update(&sma_filter, (uint32_t)raw_val);

            // Логіка гістерезису
            if (!led_state && filtered_val > THRESHOLD_DARK) {
                // Стало достатньо темно -> вмикаємо LED
                led_state = 1;
                hardware_led_set(led_state);
                ESP_LOGW(TAG, "УВІМКНЕНО LED | SMA: %" PRIu32 " (Raw: %d)", filtered_val, raw_val);
            } 
            else if (led_state && filtered_val < THRESHOLD_LIGHT) {
                // Стало достатньо світло -> вимикаємо LED
                led_state = 0;
                hardware_led_set(led_state);
                ESP_LOGI(TAG, "ВИМКНЕНО LED | SMA: %" PRIu32 " (Raw: %d)", filtered_val, raw_val);
            }
        } else {
            ESP_LOGE(TAG, "Помилка читання АЦП");
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}