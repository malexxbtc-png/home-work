#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

typedef enum {
    CLK_SRC_NONE = 0,
    CLK_SRC_BUILD,
    CLK_SRC_RTC,
    CLK_SRC_NTP,
    CLK_SRC_GPS,
} clock_src_t;

/* Reads the DS1307 and sets the system clock from it. If the RTC was never set,
 * falls back to the build time (CONFIG_WS_RTC_SET_FROM_BUILD) and writes it to the RTC. */
esp_err_t clock_sync_init(void);

/* Sets the system clock from a better source (GPS, NTP) and refreshes the DS1307
 * (at most once per hour, or right away when the RTC is missing/wrong). */
void clock_sync_set(time_t utc, clock_src_t src);

clock_src_t clock_sync_source(void);
bool        clock_sync_rtc_ok(void);
bool        clock_sync_time_valid(void);
