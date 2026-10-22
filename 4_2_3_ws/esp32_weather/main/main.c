#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_types.h"
#include "bmp280.h"
#include "clock_sync.h"
#include "gps.h"
#include "i2c_bus.h"
#include "motion.h"
#include "power.h"
#include "ssd1306.h"
#include "ui.h"
#include "uart_out.h"
#include "weather.h"
#include "wifi_time.h"

static const char *TAG = "main";

station_state_t   g_state;
SemaphoreHandle_t g_state_mtx;

/* Survives deep sleep: avoids refetching city/forecast on every PIR wake-up. */
static RTC_DATA_ATTR station_state_t s_rtc_state;
static RTC_DATA_ATTR bool            s_rtc_valid;

/* ---------- GPS callback ---------- */

static void on_gps_fix(double lat, double lon, time_t utc)
{
    clock_sync_set(utc, CLK_SRC_GPS);     /* system clock + DS1307 (rate-limited) */

    xSemaphoreTake(g_state_mtx, portMAX_DELAY);
    g_state.geo_valid = true;
    g_state.lat = lat;
    g_state.lon = lon;
    xSemaphoreGive(g_state_mtx);
}

/* ---------- Tasks ---------- */

/* Once per second: read the sensor, update the shared state, send $SENS to the ESP32-S3. */
static void sensor_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    int tick = 0;
    bool warned = false;

    for (;;) {
        float t, h, p;
        esp_err_t err = bmp280_read(&t, &h, &p);
        if (err == ESP_OK) {
            bool has_h = bmp280_has_humidity();
            xSemaphoreTake(g_state_mtx, portMAX_DELAY);
            g_state.has_sensors = true;
            g_state.temp_c = t;
            g_state.has_humidity = has_h;
            g_state.humidity = has_h ? h : 0.0f;
            g_state.pressure_hpa = p;
            xSemaphoreGive(g_state_mtx);
            uart_out_sensors(t, has_h, h, p);
            warned = false;
        } else if (!warned) {
            ESP_LOGW(TAG, "sensor read failed: %s", esp_err_to_name(err));
            warned = true;
        }

        if (++tick % 60 == 0) {            /* location + forecast once a minute (if enabled) */
            station_state_t snap;
            xSemaphoreTake(g_state_mtx, portMAX_DELAY);
            snap = g_state;
            xSemaphoreGive(g_state_mtx);
            uart_out_extra(&snap);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

static void weather_task(void *arg)
{
    const time_t refresh_s = CONFIG_WS_FORECAST_REFRESH_MIN * 60;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        station_state_t snap;
        xSemaphoreTake(g_state_mtx, portMAX_DELAY);
        snap = g_state;
        xSemaphoreGive(g_state_mtx);

        if (!snap.geo_valid || !clock_sync_time_valid()) {
            continue;   /* wait for the GPS fix */
        }
        bool stale = !snap.has_forecast || (time(NULL) - snap.forecast_time) > refresh_s;
        if (!stale && snap.city[0]) {
            continue;
        }
        if (!wifi_time_wait_connected(10000)) {
            continue;
        }

        if (!snap.city[0]) {
            char city[CITY_MAX_LEN];
            if (weather_fetch_city(snap.lat, snap.lon, city, sizeof(city)) == ESP_OK) {
                xSemaphoreTake(g_state_mtx, portMAX_DELAY);
                strlcpy(g_state.city, city, sizeof(g_state.city));
                xSemaphoreGive(g_state_mtx);
            }
        }
        if (stale) {
            forecast_day_t days[FORECAST_DAYS];
            esp_err_t err = weather_fetch_forecast(snap.lat, snap.lon, days);
            if (err == ESP_OK) {
                xSemaphoreTake(g_state_mtx, portMAX_DELAY);
                memcpy(g_state.days, days, sizeof(days));
                g_state.has_forecast = true;
                g_state.forecast_time = time(NULL);
                xSemaphoreGive(g_state_mtx);
                ESP_LOGI(TAG, "forecast updated");
            } else {
                ESP_LOGW(TAG, "forecast failed: %s", esp_err_to_name(err));
            }
        }
    }
}

/* ---------- Entry point ---------- */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    setenv("TZ", CONFIG_WS_TIMEZONE, 1);
    tzset();

    g_state_mtx = xSemaphoreCreateMutex();
    configASSERT(g_state_mtx);

    power_init();
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED && s_rtc_valid) {
        g_state = s_rtc_state;           /* restore cached city/forecast after PIR wake-up */
        g_state.has_sensors = false;
        ESP_LOGI(TAG, "state restored from RTC memory");
    }

    /* Shared I2C bus: OLED, BMP/BME280, DS1307. Missing devices are not fatal. */
    ESP_ERROR_CHECK(i2c_bus_init());
    i2c_bus_scan_log();
    if (ssd1306_init() != ESP_OK) {
        ESP_LOGW(TAG, "OLED not available");
    }
    if (bmp280_init() != ESP_OK) {
        ESP_LOGW(TAG, "BMP/BME280 not available");
    }
    clock_sync_init();                    /* system time from DS1307 right away, no WiFi needed */

    ESP_ERROR_CHECK(motion_init());
    ESP_ERROR_CHECK(uart_out_init());
    ESP_ERROR_CHECK(gps_start(on_gps_fix));
    wifi_time_start();

    ui_start();
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 4, NULL);
    xTaskCreate(weather_task, "weather", 8192, NULL, 3, NULL);

    /* Supervisor: refresh the clock after NTP syncs; deep sleep when nobody is around. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (wifi_time_take_ntp_synced()) {
            clock_sync_set(time(NULL), CLK_SRC_NTP);
        }
        if (motion_idle_seconds() >= CONFIG_WS_INACTIVITY_S) {
            xSemaphoreTake(g_state_mtx, portMAX_DELAY);
            s_rtc_state = g_state;
            s_rtc_valid = true;
            xSemaphoreGive(g_state_mtx);
            power_enter_deep_sleep();    /* returns only if the PIR is currently active */
        }
    }
}
