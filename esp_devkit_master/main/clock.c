#include "clock.h"

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "ds1307.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_wx.h"
#include "status.h"

static const char *TAG = "clock";

static bool rtc_ready;
static struct tm cached;
static bool cached_ok;
static portMUX_TYPE clock_mux = portMUX_INITIALIZER_UNLOCKED;

static bool get_build_time(struct tm *time_info)
{
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_text[4] = {0};
    int year, day, hour, minute, second, month = -1;
    if (sscanf(__DATE__, "%3s %d %d", month_text, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_text, months[i]) == 0) {
            month = i;
            break;
        }
    }
    if (month < 0) {
        return false;
    }
    *time_info = (struct tm){
        .tm_year = year - 1900, .tm_mon = month, .tm_mday = day,
        .tm_hour = hour, .tm_min = minute, .tm_sec = second, .tm_isdst = -1,
    };
    return mktime(time_info) != (time_t)-1;      /* mktime також обчислює день тижня */
}

/* Unix-час з полів struct tm, трактованих як UTC (без часової зони) */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t tm_to_epoch(const struct tm *t)
{
    return days_from_civil(t->tm_year + 1900, (unsigned)t->tm_mon + 1, (unsigned)t->tm_mday) * 86400 +
           t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

void clock_init(i2c_master_bus_handle_t bus, uint16_t address)
{
    esp_err_t err = ds1307_init(bus, address);
    rtc_ready = err == ESP_OK;
    if (!rtc_ready) {
        ESP_LOGW(TAG, "DS1307 not found: %s", esp_err_to_name(err));
        return;
    }
    struct tm rtc_time;
    err = ds1307_read_time(&rtc_time);
    time_t rtc_epoch = err == ESP_OK ? (time_t)tm_to_epoch(&rtc_time) : (time_t)-1;
    if (err == ESP_ERR_INVALID_RESPONSE || (err == ESP_OK && rtc_epoch < (time_t)VALID_EPOCH_MIN)) {
        if (get_build_time(&rtc_time) && ds1307_set_time(&rtc_time) == ESP_OK) {
            ESP_LOGW(TAG, "RTC was unset; initialized from firmware build time");
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        strcpy(status.time_src, "rtc");
    } else {
        ESP_LOGW(TAG, "DS1307 time unavailable: %s", esp_err_to_name(err));
    }
}

bool clock_get(struct tm *out)
{
    bool ok;
    portENTER_CRITICAL(&clock_mux);
    ok = cached_ok;
    if (ok && out != NULL) {
        *out = cached;
    }
    portEXIT_CRITICAL(&clock_mux);
    return ok;
}

void clock_stamp(char *buf, size_t size)
{
    struct tm t;
    if (clock_get(&t)) {
        snprintf(buf, size, "%02d.%02d %02d:%02d:%02d", t.tm_mday, t.tm_mon + 1,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        uint32_t up = status_uptime_s();
        snprintf(buf, size, "up %02u:%02u:%02u", (unsigned)(up / 3600), (unsigned)(up / 60 % 60),
                 (unsigned)(up % 60));
    }
}

/* Коригує DS1307 за часом SNTP (Wi-Fi на цій платі). net_wx_local_epoch() дає місцевий час
   (UTC + зсув з літнім часом), тому годинник записуємо без додаткових зсувів. */
static void sync_rtc_from_master(void)
{
    int64_t master_epoch;
    if (!rtc_ready || !net_wx_local_epoch(&master_epoch) || master_epoch < VALID_EPOCH_MIN) {
        return;
    }
    struct tm current;
    bool have_time = ds1307_read_time(&current) == ESP_OK;
    int64_t diff = have_time ? tm_to_epoch(&current) - master_epoch : INT64_MAX;
    if (have_time && diff >= -RTC_MAX_DRIFT_S && diff <= RTC_MAX_DRIFT_S) {
        strcpy(status.time_src, "ntp");
        return;
    }
    time_t t = (time_t)master_epoch;
    struct tm new_time;
    gmtime_r(&t, &new_time);
    if (ds1307_set_time(&new_time) == ESP_OK) {
        strcpy(status.time_src, "ntp");
        if (have_time) {
            ESP_LOGI(TAG, "RTC corrected by %lld s from NTP time", (long long)-diff);
        } else {
            ESP_LOGI(TAG, "RTC set from NTP time");
        }
    } else {
        ESP_LOGW(TAG, "Failed to write corrected time to DS1307");
        status_log_error("rtc_sync");
    }
}

static void clock_task(void *arg)
{
    (void)arg;
    TickType_t last_sync = 0;
    while (1) {
        struct tm t = {0};
        bool ok = rtc_ready && ds1307_read_time(&t) == ESP_OK;
        if (ok) {
            /* день тижня з регістра DS1307 може бути хибним, якщо його ніхто не виставляв: рахуємо з дати */
            int64_t days = days_from_civil(t.tm_year + 1900, (unsigned)t.tm_mon + 1, (unsigned)t.tm_mday);
            t.tm_wday = (int)((days + 4) % 7);               /* 1970-01-01 — четвер (4), неділя = 0 */
        }
        portENTER_CRITICAL(&clock_mux);
        cached_ok = ok;
        if (ok) {
            cached = t;
        }
        portEXIT_CRITICAL(&clock_mux);
        status.rtc_ok = ok;

        if (xTaskGetTickCount() - last_sync >= pdMS_TO_TICKS(1000)) {
            last_sync = xTaskGetTickCount();
            sync_rtc_from_master();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void clock_start(void)
{
    xTaskCreate(clock_task, "clock", 4096, NULL, 3, NULL);
}
