#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Auto-detects BMP280 (chip id 0x56..0x58) or BME280 (0x60). */
esp_err_t bmp280_init(void);

/* true only for a BME280 (BMP280 has no humidity sensor). */
bool bmp280_has_humidity(void);

/* One forced-mode measurement. humidity is set to NAN for a BMP280 (pass NULL to ignore). */
esp_err_t bmp280_read(float *temp_c, float *humidity, float *pressure_hpa);
