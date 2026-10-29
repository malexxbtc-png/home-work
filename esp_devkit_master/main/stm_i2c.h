#ifndef STM_I2C_H
#define STM_I2C_H

#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    float t;
    float rh;
    float p;
    float td;
    uint8_t heartbeat;
    uint8_t sensor_status;
    uint16_t raw_humidity;
    int16_t raw_dewpoint;
} stm32_data_t;

esp_err_t stm_i2c_init(i2c_master_bus_handle_t bus, uint16_t address);
esp_err_t stm_i2c_read(stm32_data_t *data, char *err_buf, size_t err_buf_size);

#endif
