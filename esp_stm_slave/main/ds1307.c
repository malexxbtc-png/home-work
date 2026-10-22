#include "ds1307.h"

#include <stdbool.h>
#include <stdint.h>
#include "esp_check.h"

#define DS1307_TIMEOUT_MS 100
#define DS1307_REGISTER_SECONDS 0x00
#define DS1307_REGISTER_COUNT 7

static i2c_master_dev_handle_t rtc_device;

static bool decode_bcd(uint8_t raw, uint8_t mask, uint8_t *value)
{
    raw &= mask;
    uint8_t ones = raw & 0x0F;
    uint8_t tens = (raw >> 4) & 0x0F;
    if (ones > 9 || tens > 9) {
        return false;
    }
    *value = tens * 10 + ones;
    return true;
}

static uint8_t encode_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint16_t address)
{
    if (bus == NULL || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &rtc_device),
                        "ds1307", "add RTC to I2C bus");

    esp_err_t err = i2c_master_probe(bus, address, DS1307_TIMEOUT_MS);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(rtc_device);
        rtc_device = NULL;
    }
    return err;
}

esp_err_t ds1307_read_time(struct tm *time_info)
{
    if (rtc_device == NULL || time_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t start_register = DS1307_REGISTER_SECONDS;
    uint8_t registers[DS1307_REGISTER_COUNT];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(rtc_device, &start_register,
                                                    sizeof(start_register), registers,
                                                    sizeof(registers), DS1307_TIMEOUT_MS),
                        "ds1307", "read clock registers");

    uint8_t second, minute, hour, weekday, day, month, year;
    if ((registers[0] & 0x80) != 0 ||
        !decode_bcd(registers[0], 0x7F, &second) ||
        !decode_bcd(registers[1], 0x7F, &minute) ||
        !decode_bcd(registers[3], 0x07, &weekday) ||
        !decode_bcd(registers[4], 0x3F, &day) ||
        !decode_bcd(registers[5], 0x1F, &month) ||
        !decode_bcd(registers[6], 0xFF, &year)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if ((registers[2] & 0x40) != 0) {
        uint8_t hour_12;
        if (!decode_bcd(registers[2], 0x1F, &hour_12) || hour_12 < 1 || hour_12 > 12) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        hour = (uint8_t)(hour_12 % 12 + ((registers[2] & 0x20) != 0 ? 12 : 0));
    } else if (!decode_bcd(registers[2], 0x3F, &hour)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (second > 59 || minute > 59 || hour > 23 ||
        weekday < 1 || weekday > 7 || day < 1 || day > 31 || month < 1 || month > 12) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *time_info = (struct tm){
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = month - 1,
        .tm_year = year + 100,
        .tm_wday = weekday - 1,          /* у регістрі 1 = неділя, як і при записі */
        .tm_isdst = -1,
    };
    return ESP_OK;
}

esp_err_t ds1307_set_time(const struct tm *time_info)
{
    if (rtc_device == NULL || time_info == NULL ||
        time_info->tm_year < 100 || time_info->tm_year > 199 ||
        time_info->tm_mon < 0 || time_info->tm_mon > 11 ||
        time_info->tm_mday < 1 || time_info->tm_mday > 31 ||
        time_info->tm_hour < 0 || time_info->tm_hour > 23 ||
        time_info->tm_min < 0 || time_info->tm_min > 59 ||
        time_info->tm_sec < 0 || time_info->tm_sec > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t registers[DS1307_REGISTER_COUNT + 1] = {
        DS1307_REGISTER_SECONDS,
        encode_bcd((uint8_t)time_info->tm_sec),
        encode_bcd((uint8_t)time_info->tm_min),
        encode_bcd((uint8_t)time_info->tm_hour),
        encode_bcd((uint8_t)(time_info->tm_wday + 1)),
        encode_bcd((uint8_t)time_info->tm_mday),
        encode_bcd((uint8_t)(time_info->tm_mon + 1)),
        encode_bcd((uint8_t)(time_info->tm_year - 100)),
    };
    return i2c_master_transmit(rtc_device, registers, sizeof(registers), DS1307_TIMEOUT_MS);
}
