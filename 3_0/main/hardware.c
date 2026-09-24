#include "hardware.h"
#include "config.h"
#include "driver/gpio.h"
esp_err_t hardware_init(adc_oneshot_unit_handle_t *adc_handle) {
    if (!adc_handle) return ESP_ERR_INVALID_ARG;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;
    gpio_set_level(LED_GPIO, 0);

    // Ініціалізація ADC1 (Oneshot)
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_SELECT,
    };
    err = adc_oneshot_new_unit(&init_config, adc_handle);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // Дозволяє вимірювати до ~3.3V
    };
    
    return adc_oneshot_config_channel(*adc_handle, ADC_CHANNEL_SELECT, &config);
}

esp_err_t hardware_adc_read(adc_oneshot_unit_handle_t adc_handle, int *out_raw_val) {
    if (!adc_handle || !out_raw_val) return ESP_ERR_INVALID_ARG;
    return adc_oneshot_read(adc_handle, ADC_CHANNEL_SELECT, out_raw_val);
}

void hardware_led_set(uint8_t state) {
    gpio_set_level(LED_GPIO, state ? 1 : 0);
}