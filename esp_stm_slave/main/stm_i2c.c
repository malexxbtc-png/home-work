#include "stm_i2c.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define REG_WHO_AM_I 0x00
#define REG_STATUS 0x01
#define REG_HEARTBEAT 0x02
#define REG_DATA 0x10
#define REG_CRC 0x1A
#define REGMAP_SIZE 0x1B
#define WHO_AM_I_VALUE 0x53
#define STATUS_SENSOR_OK 0x01
#define STATUS_HAS_RH 0x02
#define I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t stm_device;

static uint8_t crc8_07(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int16_t read_i16_le(const uint8_t *bytes)
{
    const uint16_t raw = read_u16_le(bytes);
    int16_t value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

esp_err_t stm_i2c_init(i2c_master_bus_handle_t bus, uint16_t address)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &config, &stm_device);
}

esp_err_t stm_i2c_read(stm32_data_t *data, char *err_buf, size_t err_buf_size)
{
    if (data == NULL || (err_buf == NULL && err_buf_size != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (err_buf_size > 0) {
        err_buf[0] = '\0';
    }

    uint8_t reg = REG_WHO_AM_I;
    uint8_t registers[REGMAP_SIZE];
    esp_err_t err = i2c_master_transmit_receive(stm_device, &reg, sizeof(reg),
                                                 registers, sizeof(registers), I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        goto read_error;
    }
    if (registers[REG_WHO_AM_I] != WHO_AM_I_VALUE) {
        err = ESP_ERR_INVALID_RESPONSE;
        if (err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "STM ID 0x%02X", registers[REG_WHO_AM_I]);
        }
        goto read_error;
    }

    const uint8_t sensor_status = registers[REG_STATUS];
    if ((sensor_status & STATUS_SENSOR_OK) == 0) {
        err = ESP_FAIL;
        if (err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "STM status 0x%02X", sensor_status);
        }
        goto read_error;
    }

    const uint8_t *block = &registers[REG_DATA];
    uint8_t expected_crc = crc8_07(block, 10);
    if (expected_crc != registers[REG_CRC]) {
        err = ESP_ERR_INVALID_CRC;
        if (err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "STM CRC %02X != %02X",
                     registers[REG_CRC], expected_crc);
        }
        goto read_error;
    }

    const int16_t temperature = read_i16_le(&block[0]);
    const uint16_t humidity = read_u16_le(&block[2]);
    const uint32_t pressure_pa = (uint32_t)block[4]
        | ((uint32_t)block[5] << 8)
        | ((uint32_t)block[6] << 16)
        | ((uint32_t)block[7] << 24);
    const int16_t dewpoint = read_i16_le(&block[8]);

    data->t = temperature / 100.0f;
    data->rh = (sensor_status & STATUS_HAS_RH) && humidity != UINT16_MAX
        ? humidity / 100.0f : NAN;
    data->p = pressure_pa / 100.0f;
    data->td = dewpoint == INT16_MIN ? -32768.0f : dewpoint / 100.0f;
    data->heartbeat = registers[REG_HEARTBEAT];
    data->sensor_status = sensor_status;
    data->raw_humidity = humidity;
    data->raw_dewpoint = dewpoint;
    return ESP_OK;

read_error:
    if (err_buf_size > 0 && err_buf[0] == '\0') {
        snprintf(err_buf, err_buf_size, "STM I2C read: %s", esp_err_to_name(err));
    }
    return err;
}
