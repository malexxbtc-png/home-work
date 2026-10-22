#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

typedef void (*gps_fix_cb_t)(double lat, double lon, time_t utc);

/* Starts the UART reader task. The callback runs in that task for every valid RMC sentence. */
esp_err_t gps_start(gps_fix_cb_t cb);

/* Switches the GPS supply via CONFIG_WS_GPS_EN (no-op if it is -1). */
void gps_power(bool on);

/* true if a valid fix arrived within the last 5 seconds. */
bool gps_has_recent_fix(void);
