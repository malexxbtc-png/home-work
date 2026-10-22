#include "mpu6050.h"

#include <math.h>

#define MPU6050_REG_WHO_AM_I 0x75
#define MPU6050_REG_POWER_MGMT_1 0x6B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_TIMEOUT_MS 100

static i2c_master_dev_handle_t mpu_device;

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    const uint8_t command[] = {reg, value};
    return i2c_master_transmit(mpu_device, command, sizeof(command), MPU6050_TIMEOUT_MS);
}

esp_err_t mpu6050_init(i2c_master_bus_handle_t bus, uint16_t address)
{
    if (bus == NULL || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_master_probe(bus, address, MPU6050_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &config, &mpu_device);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t identity = 0;
    const uint8_t identity_register = MPU6050_REG_WHO_AM_I;
    err = i2c_master_transmit_receive(mpu_device, &identity_register, sizeof(identity_register),
                                     &identity, sizeof(identity), MPU6050_TIMEOUT_MS);
    if (err == ESP_OK && identity != 0x68) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_POWER_MGMT_1, 0x00);
    }
    if (err == ESP_OK) {
        err = write_register(MPU6050_REG_ACCEL_CONFIG, 0x18);
    }
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(mpu_device);
        mpu_device = NULL;
    }
    return err;
}

esp_err_t mpu6050_get_accel_angles(float *tilt_degrees, float *shock_g)
{
    if (mpu_device == NULL || tilt_degrees == NULL || shock_g == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = MPU6050_REG_ACCEL_XOUT_H;
    uint8_t raw[6];
    esp_err_t err = i2c_master_transmit_receive(mpu_device, &reg, sizeof(reg), raw, sizeof(raw), MPU6050_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    const int16_t ax_raw = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    const int16_t ay_raw = (int16_t)(((uint16_t)raw[2] << 8) | raw[3]);
    const int16_t az_raw = (int16_t)(((uint16_t)raw[4] << 8) | raw[5]);
    const float ax = ax_raw / 2048.0f;
    const float ay = ay_raw / 2048.0f;
    const float az = az_raw / 2048.0f;
    const float magnitude = sqrtf(ax * ax + ay * ay + az * az);

    *tilt_degrees = atan2f(sqrtf(ax * ax + ay * ay), fabsf(az)) * 57.2957795131f;
    *shock_g = fabsf(magnitude - 1.0f);
    return ESP_OK;
}
