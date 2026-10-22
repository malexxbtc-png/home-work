#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t motion_init(void);

/* Seconds since the PIR last reported motion (0 while its output is still high). */
uint32_t motion_idle_seconds(void);
