#pragma once

#include <time.h>
#include "esp_err.h"

/* DS1307 (Tiny RTC module). The chip stores UTC, 24-hour mode. */
esp_err_t ds1307_init(void);

/* ESP_ERR_INVALID_STATE: oscillator halted / never set; ESP_ERR_INVALID_RESPONSE: implausible date. */
esp_err_t ds1307_read_utc(time_t *out);
esp_err_t ds1307_write_utc(time_t utc);
