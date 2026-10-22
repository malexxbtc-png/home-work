#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_types.h"
#include "clock_sync.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps.h"
#include "ssd1306.h"
#include "weather.h"
#include "wifi_time.h"

typedef enum { SCR_TIME, SCR_TEMP, SCR_HUM, SCR_PRES, SCR_CITY, SCR_FCST } screen_t;
typedef struct { screen_t id; uint32_t ms; } slot_t;

static const char *TAG = "ui";
static volatile bool s_halt;

static const char *WDAY3[]     = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *WDAY_FULL[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
                                  "THURSDAY", "FRIDAY", "SATURDAY"};

static int weekday_of(const char *ymd)
{
    int y, m, d;
    if (sscanf(ymd, "%d-%d-%d", &y, &m, &d) != 3) {
        return 0;
    }
    struct tm t = {0};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    mktime(&t);
    return t.tm_wday;
}

static void scr_time(void)
{
    char buf[32];

    /* Left: WiFi (W connected, w connecting, X no SSID) and GPS (G fix, g searching). */
    char wf = ' ';
    switch (wifi_time_state()) {
    case WIFI_ST_NO_SSID:    wf = 'X'; break;
    case WIFI_ST_CONNECTING: wf = 'w'; break;
    case WIFI_ST_CONNECTED:  wf = 'W'; break;
    default: break;
    }
    snprintf(buf, sizeof(buf), "%c %c", wf, gps_has_recent_fix() ? 'G' : 'g');
    ssd1306_text(0, 0, 1, buf);
    /* Right: RTC (R ok, ! problem). */
    ssd1306_text_right(0, 1, clock_sync_rtc_ok() ? "R" : "!");

    if (!clock_sync_time_valid()) {
        ssd1306_text_center(10, 4, "--:--");
        ssd1306_text_center(40, 2, "--.--.----");
        return;
    }
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);

    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    ssd1306_text_center(10, 4, buf);
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
    ssd1306_text_center(40, 2, buf);
    ssd1306_text(0, 56, 1, WDAY_FULL[lt.tm_wday]);
    snprintf(buf, sizeof(buf), "%02d", lt.tm_sec);
    ssd1306_text_right(56, 1, buf);
}

static void scr_value(const char *title, const char *value, int scale)
{
    ssd1306_text_center(2, 1, title);
    ssd1306_hline(0, 12, OLED_W);
    ssd1306_text_center(28, scale, value);
}

static void scr_pressure(const station_state_t *s)
{
    char buf[24];
    ssd1306_text_center(2, 1, "PRESSURE");
    ssd1306_hline(0, 12, OLED_W);
    snprintf(buf, sizeof(buf), "%.0f hPa", s->pressure_hpa);
    ssd1306_text_center(22, 2, buf);
    snprintf(buf, sizeof(buf), "%.0f mmHg", s->pressure_hpa * 0.750062f);
    ssd1306_text_center(42, 2, buf);
}

static void scr_city(const station_state_t *s)
{
    char buf[24];
    int scale = ssd1306_text_width(2, s->city) <= OLED_W ? 2 : 1;
    ssd1306_text_center(8, scale, s->city);
    snprintf(buf, sizeof(buf), "%.4f%c", fabs(s->lat), s->lat >= 0 ? 'N' : 'S');
    ssd1306_text_center(30, 2, buf);
    snprintf(buf, sizeof(buf), "%.4f%c", fabs(s->lon), s->lon >= 0 ? 'E' : 'W');
    ssd1306_text_center(48, 2, buf);
}

static void scr_forecast(const station_state_t *s)
{
    char buf[32];
    for (int i = 0; i < FORECAST_DAYS; i++) {
        const forecast_day_t *f = &s->days[i];
        int y = 2 + i * 21;
        snprintf(buf, sizeof(buf), "%s %.5s  %.0f/%.0fC", WDAY3[weekday_of(f->date)],
                 f->date + 5, f->tmin, f->tmax);
        ssd1306_text(0, y, 1, buf);
        ssd1306_text(0, y + 9, 1, weather_wmo_text(f->wmo_code));
    }
}

static int build_slots(const station_state_t *s, slot_t *slots)
{
    const uint32_t sh = CONFIG_WS_SHORT_SCREEN_MS;
    int n = 0;
    slots[n++] = (slot_t){SCR_TIME, sh * 4};
    if (s->has_sensors) {
        slots[n++] = (slot_t){SCR_TEMP, sh};
        if (s->has_humidity) {
            slots[n++] = (slot_t){SCR_HUM, sh};
        }
        slots[n++] = (slot_t){SCR_PRES, sh};
    }
    if (s->geo_valid && s->city[0]) {
        slots[n++] = (slot_t){SCR_CITY, sh};
    }
    if (s->has_forecast) {
        slots[n++] = (slot_t){SCR_FCST, sh};
    }
    return n;
}

static void ui_task(void *arg)
{
    for (;;) {
        if (s_halt) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        station_state_t s;
        xSemaphoreTake(g_state_mtx, portMAX_DELAY);
        s = g_state;
        xSemaphoreGive(g_state_mtx);

        slot_t slots[6];
        int n = build_slots(&s, slots);
        uint32_t total = 0;
        for (int i = 0; i < n; i++) {
            total += slots[i].ms;
        }
        uint32_t t = (uint32_t)(esp_timer_get_time() / 1000) % total;
        screen_t cur = SCR_TIME;
        for (int i = 0; i < n; i++) {
            if (t < slots[i].ms) {
                cur = slots[i].id;
                break;
            }
            t -= slots[i].ms;
        }

        char buf[24];
        ssd1306_clear();
        switch (cur) {
        case SCR_TIME:
            scr_time();
            break;
        case SCR_TEMP:
            snprintf(buf, sizeof(buf), "%.1f C", s.temp_c);
            scr_value("TEMPERATURE", buf, 3);
            break;
        case SCR_HUM:
            snprintf(buf, sizeof(buf), "%.0f %%", s.humidity);
            scr_value("HUMIDITY", buf, 3);
            break;
        case SCR_PRES:
            scr_pressure(&s);
            break;
        case SCR_CITY:
            scr_city(&s);
            break;
        case SCR_FCST:
            scr_forecast(&s);
            break;
        }
        if (ssd1306_flush() != ESP_OK) {
            ESP_LOGW(TAG, "display write failed");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t ui_start(void)
{
    s_halt = false;
    return xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void ui_sleep(void)
{
    s_halt = true;
    vTaskDelay(pdMS_TO_TICKS(150));      /* let a frame in flight finish */
    ssd1306_power(false);
}
