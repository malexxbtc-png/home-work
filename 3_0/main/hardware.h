#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t hardware_init(adc_oneshot_unit_handle_t *adc_handle);
esp_err_t hardware_adc_read(adc_oneshot_unit_handle_t adc_handle, int *out_raw_val);
void hardware_led_set(uint8_t state);

#endif // HARDWARE_H