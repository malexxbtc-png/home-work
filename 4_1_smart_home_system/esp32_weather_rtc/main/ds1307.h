#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * Драйвер DS1307 (модуль Tiny RTC I2C, адреса 0x68; EEPROM 24C32 на 0x50 не чіпаємо).
 * В RTC зберігається UTC - літній/зимовий час рахується програмно через TZ.
 */

/** Додає DS1307 на шину і перевіряє, що він відповідає. */
esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * Прочитати час (UTC).
 *  ESP_OK                - час коректний
 *  ESP_ERR_INVALID_STATE - генератор зупинений (біт CH)
 *  ESP_ERR_INVALID_RESPONSE - некоректні дані в регістрах RTC
 *  інші                  - помилка I2C
 */
esp_err_t ds1307_read_utc(time_t *out);

/** Записати час (UTC) і запустити генератор. */
esp_err_t ds1307_write_utc(time_t t);
