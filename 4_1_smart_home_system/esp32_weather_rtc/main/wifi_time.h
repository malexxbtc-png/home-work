#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * WiFi + SNTP тепер лише ДОДАТКОВЕ джерело: годинник іде від DS1307,
 * а NTP раз на годину підправляє RTC (якщо WiFi підключився).
 */

typedef enum {
    WIFI_ST_OFF,            /* вимкнено в menuconfig */
    WIFI_ST_NOT_CONFIGURED, /* SSID не задано (лишився YOUR_WIFI_SSID) */
    WIFI_ST_CONNECTING,     /* ще не підключено (див. причину в логах) */
    WIFI_ST_CONNECTED,      /* є IP */
} wifi_st_t;

/** Запускає WiFi (STA) і SNTP у фоні. Не блокує. Не валить програму при помилках. */
void wifi_time_start(void);

wifi_st_t wifi_time_status(void);

/** true + UTC час, якщо SNTP вже синхронізував системний годинник. */
bool wifi_time_get_utc(time_t *out);
