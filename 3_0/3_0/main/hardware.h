#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t hardware_init(adc_oneshot_unit_handle_t *adc_handle);
int hardware_adc_read(adc_oneshot_unit_handle_t adc_handle);
void hardware_led_set(uint8_t state);