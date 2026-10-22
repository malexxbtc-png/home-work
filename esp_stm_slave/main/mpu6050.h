#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t mpu6050_init(i2c_master_bus_handle_t bus, uint16_t address);
esp_err_t mpu6050_get_accel_angles(float *tilt_degrees, float *shock_g);

#endif
