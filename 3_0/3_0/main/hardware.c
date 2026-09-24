#include "hardware.h"
#include "config.h"

esp_err_t hardware_init(adc_oneshot_unit_handle_t *adc_handle) {
    // Ініціалізація GPIO для світлодіода
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    // Конфігурація АЦП блоку ADC2
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_2,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    return adc_oneshot_config_channel(*adc_handle, ADC_CHANNEL_SELECT, &config);
}

int hardware_adc_read(adc_oneshot_unit_handle_t adc_handle) {
    int raw_val = 0;
    adc_oneshot_read(adc_handle, ADC_CHANNEL_SELECT, &raw_val);
    return raw_val;
}

void hardware_led_set(uint8_t state) {
    gpio_set_level(LED_GPIO, state ? 1 : 0);
}