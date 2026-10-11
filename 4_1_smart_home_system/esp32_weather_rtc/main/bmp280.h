#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    float temperature;   /* °C */
    float pressure;      /* гПа */
    float humidity;      /* %RH, лише для BME280 */
    bool  has_humidity;
} bmp280_data_t;

/**
 * Ініціалізація BMP280/BME280. Пробує адресу addr, а потім другу (0x76 <-> 0x77).
 * Тип чіпа визначається автоматично за chip ID (0x58 = BMP280, 0x60 = BME280).
 */
esp_err_t bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr);

/** true, якщо підключений BME280 (є вологість). */
bool bmp280_has_humidity(void);

/** Прочитати вимірювання. */
esp_err_t bmp280_read(bmp280_data_t *out);
