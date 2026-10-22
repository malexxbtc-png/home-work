#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/* One shared I2C bus (OLED, BMP/BME280, DS1307). */
esp_err_t i2c_bus_init(void);
esp_err_t i2c_bus_add(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out);
void      i2c_bus_scan_log(void);
