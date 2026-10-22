#include "clock_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "ds1307.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define RTC_WRITE_INTERVAL_S 3600

static const char *TAG = "clock";

static SemaphoreHandle_t s_mtx;
static clock_src_t s_src;
static bool s_rtc_ok;
static bool s_valid;
static time_t s_last_rtc_write;

static void set_system_time(time_t utc)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    if (llabs((long long)now.tv_sec - (long long)utc) > 1) {   /* avoid needless time jumps */
        struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
        settimeofday(&tv, NULL);
    }
}

/* __DATE__/__TIME__ are the builder's local time: convert with the configured TZ. */
static time_t build_time_utc(void)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int d = 1, y = 2024, hh = 0, mm = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
    const char *p = strstr(months, mon);

    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon = p ? (int)(p - months) / 3 : 0;
    t.tm_mday = d;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    t.tm_isdst = -1;
    return mktime(&t);
}

esp_err_t clock_sync_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ds1307_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS1307 not found: %s", esp_err_to_name(err));
        s_rtc_ok = false;
    } else {
        time_t t;
        err = ds1307_read_utc(&t);
        if (err == ESP_OK) {
            set_system_time(t);
            s_src = CLK_SRC_RTC;
            s_valid = true;
            s_rtc_ok = true;
            s_last_rtc_write = t;
            ESP_LOGI(TAG, "time from DS1307");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "DS1307 time not valid (%s)", esp_err_to_name(err));
    }

#if CONFIG_WS_RTC_SET_FROM_BUILD
    time_t b = build_time_utc();
    set_system_time(b);
    s_src = CLK_SRC_BUILD;
    s_valid = true;
    if (ds1307_write_utc(b) == ESP_OK) {
        s_rtc_ok = true;
        s_last_rtc_write = b;
    }
    ESP_LOGW(TAG, "using build time until GPS/NTP provides the real one");
#endif
    return ESP_OK;
}

void clock_sync_set(time_t utc, clock_src_t src)
{
    if (!s_mtx) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    set_system_time(utc);
    s_src = src;
    s_valid = true;

    if (!s_rtc_ok || (utc - s_last_rtc_write) >= RTC_WRITE_INTERVAL_S ||
        (s_last_rtc_write - utc) >= RTC_WRITE_INTERVAL_S) {
        esp_err_t err = ds1307_write_utc(utc);
        s_rtc_ok = (err == ESP_OK);
        if (s_rtc_ok) {
            s_last_rtc_write = utc;
            ESP_LOGI(TAG, "DS1307 updated");
        } else {
            ESP_LOGW(TAG, "DS1307 write failed: %s", esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_mtx);
}

clock_src_t clock_sync_source(void)     { return s_src; }
bool        clock_sync_rtc_ok(void)     { return s_rtc_ok; }
bool        clock_sync_time_valid(void) { return s_valid; }
