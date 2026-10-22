#include "gps.h"

#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "timeutil.h"

#define GPS_UART      UART_NUM_1
#define GPS_BAUD      9600
#define LINE_MAX_LEN  128

static const char *TAG = "gps";
static gps_fix_cb_t s_cb;
static volatile uint32_t s_last_fix_ms;    /* 32-bit: atomic between task and readers */
static volatile bool s_ever_fixed;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Validates "$....*HH" checksum. */
static bool nmea_checksum_ok(const char *s)
{
    if (s[0] != '$') {
        return false;
    }
    uint8_t sum = 0;
    const char *p = s + 1;
    while (*p && *p != '*') {
        sum ^= (uint8_t)*p++;
    }
    if (*p != '*') {
        return false;
    }
    return sum == (uint8_t)strtol(p + 1, NULL, 16);
}

/* ddmm.mmmm -> decimal degrees */
static double nmea_to_deg(const char *v, char hemi)
{
    double raw = atof(v);
    int deg = (int)(raw / 100.0);
    double d = deg + (raw - deg * 100.0) / 60.0;
    return (hemi == 'S' || hemi == 'W') ? -d : d;
}

static time_t utc_from_fields(const char *hhmmss, const char *ddmmyy)
{
    if (strlen(hhmmss) < 6 || strlen(ddmmyy) < 6) {
        return 0;
    }
    struct tm t = {0};
    t.tm_hour = (hhmmss[0] - '0') * 10 + (hhmmss[1] - '0');
    t.tm_min  = (hhmmss[2] - '0') * 10 + (hhmmss[3] - '0');
    t.tm_sec  = (hhmmss[4] - '0') * 10 + (hhmmss[5] - '0');
    t.tm_mday = (ddmmyy[0] - '0') * 10 + (ddmmyy[1] - '0');
    t.tm_mon  = (ddmmyy[2] - '0') * 10 + (ddmmyy[3] - '0') - 1;
    t.tm_year = 100 + (ddmmyy[4] - '0') * 10 + (ddmmyy[5] - '0');
    return tm_to_utc(&t);
}

/* $xxRMC,time,status,lat,N/S,lon,E/W,speed,course,date,... */
static void parse_rmc(char *line)
{
    char *f[13] = {0};
    int n = 0;
    char *star = strchr(line, '*');
    if (star) {
        *star = '\0';
    }
    for (char *p = line; n < 13; ) {
        f[n++] = p;
        p = strchr(p, ',');
        if (!p) {
            break;
        }
        *p++ = '\0';
    }
    if (n < 10 || f[2][0] != 'A' || !f[3][0] || !f[5][0]) {
        return; /* no fix yet */
    }
    double lat = nmea_to_deg(f[3], f[4][0]);
    double lon = nmea_to_deg(f[5], f[6][0]);
    time_t utc = utc_from_fields(f[1], f[9]);
    if (utc > 0) {
        s_last_fix_ms = now_ms();
        s_ever_fixed = true;
        if (s_cb) {
            s_cb(lat, lon, utc);
        }
    }
}

static void gps_task(void *arg)
{
    char line[LINE_MAX_LEN];
    size_t len = 0;
    uint8_t chunk[64];

    for (;;) {
        int r = uart_read_bytes(GPS_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(500));
        for (int i = 0; i < r; i++) {
            char c = (char)chunk[i];
            if (c == '\n' || c == '\r') {
                if (len > 6) {
                    line[len] = '\0';
                    /* GP/GN/BD talker + "RMC" */
                    if (nmea_checksum_ok(line) && strncmp(line + 3, "RMC", 3) == 0) {
                        parse_rmc(line);
                    }
                }
                len = 0;
            } else if (len < LINE_MAX_LEN - 1) {
                line[len++] = c;
            } else {
                len = 0; /* overflow: drop the line */
            }
        }
    }
}

void gps_power(bool on)
{
#if CONFIG_WS_GPS_EN >= 0
    gpio_set_direction(CONFIG_WS_GPS_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_WS_GPS_EN, on ? 1 : 0);
#else
    (void)on;
#endif
}

bool gps_has_recent_fix(void)
{
    return s_ever_fixed && (now_ms() - s_last_fix_ms) < 5000;
}

esp_err_t gps_start(gps_fix_cb_t cb)
{
    s_cb = cb;
    gps_power(true);

    const uart_config_t cfg = {
        .baud_rate = GPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART, CONFIG_WS_GPS_TX, CONFIG_WS_GPS_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    if (xTaskCreate(gps_task, "gps", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
