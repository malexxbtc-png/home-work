#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WIFI_ST_OFF = 0,      /* disabled in menuconfig */
    WIFI_ST_NO_SSID,      /* SSID not set */
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,    /* has an IP */
} wifi_state_t;

/* Starts WiFi (station) and SNTP. Does nothing if disabled or the SSID is not set. */
esp_err_t wifi_time_start(void);
void      wifi_time_stop(void);

wifi_state_t wifi_time_state(void);
bool         wifi_time_wait_connected(uint32_t timeout_ms);

/* true once after every successful SNTP sync (system clock already updated). */
bool wifi_time_take_ntp_synced(void);
