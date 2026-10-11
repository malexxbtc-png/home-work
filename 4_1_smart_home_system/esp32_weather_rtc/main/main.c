/*
 * ESP32 DevKit: метеостанція
 *  - OLED 0.96" SSD1306 128x64 (I2C) + BMP280/BME280 (I2C, спільна шина)
 *  - годинник DS1307 (Tiny RTC, I2C 0x68) - основне джерело часу; WiFi/NTP лише підстроює RTC
 *  - екрани по колу: ЧАС(+день, дата) -> температура -> вологість -> тиск
 *  - екран часу показується в 4 рази довше
 *  - дані датчика раз на секунду йдуть по UART2 на ESP32-S3
 *
 * Формат UART (як NMEA):  $SENS,<temp C>,<hum %|NA>,<press hPa>*<XOR hex>\r\n
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_idf_version.h"

#include "ssd1306.h"
#include "bmp280.h"
#include "ds1307.h"
#include "wifi_time.h"

static const char *TAG = "main";

#define UART_PORT        UART_NUM_2
#define SHORT_SCREEN_MS  CONFIG_WS_SHORT_SCREEN_MS
#define TIME_SCREEN_MS   (4 * SHORT_SCREEN_MS)   /* час - у 4 рази довше */
#define DRAW_PERIOD_MS   250
#define SENSOR_PERIOD_MS 1000
#define RTC_FAILURE_LIMIT 10

typedef enum { SCR_TIME, SCR_TEMP, SCR_HUM, SCR_PRESS, SCR_COUNT } screen_t;

static bmp280_data_t s_data;
static bool s_sensor_ok;
static bool s_data_valid;

/* ---- час ---- */
#define NTP_TO_RTC_PERIOD_MS  (3600u * 1000u)   /* як часто записувати NTP-час в RTC */
static bool     s_rtc_ok;          /* DS1307 знайдено на шині */
static bool     s_rtc_valid;       /* зв'язок із RTC справний; вимикається після повторних збоїв */
static bool     s_time_valid;      /* s_utc можна показувати */
static time_t   s_utc;             /* поточний час UTC */
static bool     s_ntp_written;
static uint32_t s_ntp_written_at;
static bool     s_build_set_done;
static time_t   s_clock_base_utc;
static uint32_t s_clock_base_ms;
static bool     s_clock_started;
static uint8_t  s_rtc_failures;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------- Час (DS1307 + NTP) ---------------- */

static void clock_set_base(time_t utc, uint32_t now)
{
    s_clock_base_utc = utc;
    s_clock_base_ms = now;
    s_clock_started = true;
    s_utc = utc;
    s_time_valid = true;
}

static bool clock_update_from_base(uint32_t now)
{
    if (!s_clock_started) {
        return false;
    }
    s_utc = s_clock_base_utc + (now - s_clock_base_ms) / 1000;
    s_time_valid = true;
    return true;
}

static void recover_i2c_bus(i2c_master_bus_handle_t bus, const char *source)
{
    esp_err_t err = i2c_master_bus_reset(bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus reset after %s failure failed: %s",
                 source, esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "I2C bus reset after %s failure", source);
    }
}

/* час збірки (локальний, TZ з menuconfig) -> UTC. Запасний варіант, поки немає NTP */
static bool build_time_utc(time_t *out)
{
    static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char m[4] = { 0 };
    int d, y, hh, mm, ss;
    if (sscanf(__DATE__, "%3s %d %d", m, &d, &y) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) {
        return false;
    }
    const char *p = strstr(mons, m);
    if (!p) {
        return false;
    }
    struct tm t = { .tm_year = y - 1900, .tm_mon = (int)(p - mons) / 3, .tm_mday = d,
                    .tm_hour = hh, .tm_min = mm, .tm_sec = ss, .tm_isdst = -1 };
    *out = mktime(&t);
    return *out != (time_t)-1;
}

static esp_err_t rtc_write(time_t t, i2c_master_bus_handle_t bus, uint32_t now)
{
    esp_err_t err = ds1307_write_utc(t);
    if (err == ESP_OK) {
        clock_set_base(t, now);
        s_rtc_valid = true;
        s_rtc_failures = 0;
    } else {
        ESP_LOGW(TAG, "RTC write failed: %s", esp_err_to_name(err));
        recover_i2c_bus(bus, "RTC write");
    }
    return err;
}

/* викликається раз на секунду */
static void time_update(uint32_t now, i2c_master_bus_handle_t bus)
{
    time_t t;

#if CONFIG_WS_RTC_SET_FROM_BUILD
    if (s_rtc_ok && !s_build_set_done) {
        s_build_set_done = true;
        if (build_time_utc(&t)) {
            ESP_LOGW(TAG, "RTC примусово виставлено часом збірки");
            rtc_write(t, bus, now);
        }
    }
#endif

    /* 1) NTP (якщо є) - найточніше; раз на годину пишемо в RTC */
    time_t ntp;
    bool have_ntp = wifi_time_get_utc(&ntp);
    if (have_ntp && s_rtc_ok &&
        (!s_ntp_written || now - s_ntp_written_at >= NTP_TO_RTC_PERIOD_MS)) {
        rtc_write(ntp, bus, now);
        s_ntp_written = true;
        s_ntp_written_at = now;
    }

    /* 2) читаємо RTC */
    if (s_rtc_ok) {
        esp_err_t err = ds1307_read_utc(&t);
        if (err == ESP_OK) {
            s_rtc_valid = true;
            s_rtc_failures = 0;
            clock_set_base(t, now);
            return;
        }
        if (s_rtc_failures < UINT8_MAX) {
            s_rtc_failures++;
        }
        if (s_rtc_failures >= RTC_FAILURE_LIMIT && s_rtc_valid) {
            s_rtc_valid = false;
            ESP_LOGW(TAG, "RTC unavailable after %u consecutive read failures",
                     s_rtc_failures);
        }
        if (err == ESP_ERR_INVALID_STATE && !s_build_set_done && !have_ntp) {
            /* RTC ще не заведений (нова батарейка) - ставимо час збірки, NTP потім уточнить */
            s_build_set_done = true;
            if (build_time_utc(&t)) {
                ESP_LOGW(TAG, "RTC не виставлений - пишу час збірки");
                rtc_write(t, bus, now);
            }
        } else if (err != ESP_ERR_INVALID_STATE && err != ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(err));
            recover_i2c_bus(bus, "RTC read");
        }
    }

    /* 3) RTC недоступний - запасний варіант: системний годинник від NTP */
    if (have_ntp) {
        clock_set_base(ntp, now);
    } else if (!clock_update_from_base(now)) {
        s_time_valid = false;
    }
}

/* ---------------- UART ---------------- */

static void uart_setup(void)
{
    const uart_config_t cfg = {
        .baud_rate = CONFIG_WS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, CONFIG_WS_UART_TX, CONFIG_WS_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void uart_send_data(const bmp280_data_t *d)
{
    char hum[12];
    if (d->has_humidity) {
        snprintf(hum, sizeof(hum), "%.1f", d->humidity);
    } else {
        strcpy(hum, "NA");
    }

    char body[64];
    snprintf(body, sizeof(body), "SENS,%.2f,%s,%.2f", d->temperature, hum, d->pressure);

    uint8_t cs = 0;
    for (const char *p = body; *p; p++) {
        cs ^= (uint8_t)*p;
    }

    char line[80];
    int n = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
    uart_write_bytes(UART_PORT, line, n);
    ESP_LOGI(TAG, "UART TX: $%s*%02X", body, cs);
}

/* ---------------- Екрани ---------------- */

static void draw_time_screen(void)
{
    ssd1306_clear();
    if (s_time_valid) {
        struct tm t;
        localtime_r(&s_utc, &t);   /* UTC -> місцевий час з літнім/зимовим */
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        ssd1306_draw_centered(2, buf, 2);

        strftime(buf, sizeof(buf), "%A", &t);        /* день тижня (англ.) */
        ssd1306_draw_centered(26, buf, 2);

        strftime(buf, sizeof(buf), "%d.%m.%Y", &t);  /* дата */
        ssd1306_draw_centered(47, buf, 2);
    } else {
        ssd1306_draw_centered(2, "--:--:--", 2);
        ssd1306_draw_centered(26, s_rtc_ok ? "RTC not set" : "RTC not found", 1);
        ssd1306_draw_centered(38, "check wiring 5V/SDA/SCL", 1);
    }

    /* індикатори в кутах: ліворуч WiFi (W - є IP, w - підключається, X - не налаштовано),
       праворуч RTC (R - ок, ! - проблема) */
    switch (wifi_time_status()) {
    case WIFI_ST_CONNECTED:      ssd1306_draw_string(0, 2, "W", 1); break;
    case WIFI_ST_CONNECTING:     ssd1306_draw_string(0, 2, "w", 1); break;
    case WIFI_ST_NOT_CONFIGURED: ssd1306_draw_string(0, 2, "X", 1); break;
    default: break;
    }
    ssd1306_draw_string(SSD1306_WIDTH - 6, 2, s_rtc_valid ? "R" : "!", 1);
    ssd1306_flush();
}

static void draw_value_screen(const char *label, bool valid, float value,
                              int decimals, const char *unit, const char *hint)
{
    ssd1306_clear();
    ssd1306_draw_centered(2, label, 1);

    if (!valid) {
        ssd1306_draw_centered(24, "N/A", 3);
        if (hint) {
            ssd1306_draw_centered(54, hint, 1);
        }
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        ssd1306_draw_centered(22, buf, 3);
        ssd1306_draw_centered(52, unit, 1);
    }
    ssd1306_flush();
}

static void draw_screen(screen_t s)
{
    switch (s) {
    case SCR_TIME:
        draw_time_screen();
        break;
    case SCR_TEMP:
        draw_value_screen("TEMPERATURE", s_data_valid, s_data.temperature, 1, "deg C", NULL);
        break;
    case SCR_HUM:
        draw_value_screen("HUMIDITY", s_data_valid && s_data.has_humidity, s_data.humidity, 1, "%",
                          "needs BME280");
        break;
    case SCR_PRESS:
        draw_value_screen("PRESSURE", s_data_valid, s_data.pressure, 1, "hPa", NULL);
        break;
    default:
        break;
    }
}

static screen_t next_screen(screen_t s)
{
    do {
        s = (screen_t)((s + 1) % SCR_COUNT);
        /* BMP280 не має вологості - пропускаємо цей екран */
    } while (s == SCR_HUM && s_sensor_ok && !bmp280_has_humidity());
    return s;
}

/* ---------------- main ---------------- */

void app_main(void)
{
    /* I2C шина (спільна для OLED і датчика) */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_WS_I2C_SDA,
        .scl_io_num = CONFIG_WS_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* на модулях зазвичай є свої підтяжки */
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    /* часовий пояс: RTC зберігає UTC, на екрані - місцевий час */
    setenv("TZ", CONFIG_WS_TIMEZONE, 1);
    tzset();

    /* діагностика: які пристрої відповідають на шині */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    {
        char found[96] = "";
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (i2c_master_probe(bus, a, 20) == ESP_OK) {
                size_t l = strlen(found);
                snprintf(found + l, sizeof(found) - l, " 0x%02X", a);
            }
        }
        ESP_LOGI(TAG, "I2C devices:%s", found[0] ? found : " (нічого!)");
        ESP_LOGI(TAG, "очікується: OLED 0x3C, BMP/BME 0x76|0x77, DS1307 0x68, EEPROM 24C32 0x50");
    }
#endif

    /* дисплей */
    esp_err_t err = ssd1306_init(bus, CONFIG_WS_OLED_ADDR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed (%s). Перевір підключення / адресу 0x3C-0x3D",
                 esp_err_to_name(err));
    } else {
        ssd1306_draw_centered(28, "Starting...", 1);
        ssd1306_flush();
    }

    /* датчик */
    s_sensor_ok = (bmp280_init(bus, CONFIG_WS_SENSOR_ADDR) == ESP_OK);
    if (!s_sensor_ok) {
        ESP_LOGE(TAG, "BMP280/BME280 не знайдено. Перевір підключення / адресу 0x76-0x77");
    }

    /* годинник DS1307 */
    s_rtc_ok = (ds1307_init(bus, CONFIG_WS_RTC_ADDR) == ESP_OK);
    s_rtc_valid = s_rtc_ok;
    if (!s_rtc_ok) {
        ESP_LOGE(TAG, "DS1307 не знайдено. Живлення модуля 5V (VCC), SDA/SCL, адреса 0x68");
    }

    uart_setup();
    wifi_time_start();   /* WiFi + SNTP у фоні, не блокує */

    screen_t screen = SCR_TIME;
    uint32_t screen_start = now_ms();
    uint32_t last_draw = 0, last_sensor = 0, last_time = 0;
    bool first = true;

    while (true) {
        uint32_t now = now_ms();

        /* оновлення часу раз на секунду */
        if (first || now - last_time >= 1000) {
            last_time = now;
            time_update(now, bus);
        }

        /* читання датчика + відправка по UART раз на секунду */
        if (first || now - last_sensor >= SENSOR_PERIOD_MS) {
            last_sensor = now;
            if (s_sensor_ok) {
                bmp280_data_t d;
                if (bmp280_read(&d) == ESP_OK) {
                    s_data = d;
                    s_data_valid = true;
                    uart_send_data(&s_data);
                } else {
                    ESP_LOGW(TAG, "sensor read failed");
                    recover_i2c_bus(bus, "sensor read");
                }
            }
        }

        /* зміна екрана: час показується в 4 рази довше */
        uint32_t duration = (screen == SCR_TIME) ? TIME_SCREEN_MS : SHORT_SCREEN_MS;
        if (now - screen_start >= duration) {
            screen = next_screen(screen);
            screen_start = now;
            last_draw = 0;   /* одразу перемалювати */
        }

        /* перемальовування 4 рази/сек (щоб секунди йшли плавно) */
        if (first || now - last_draw >= DRAW_PERIOD_MS) {
            last_draw = now;
            draw_screen(screen);
        }

        first = false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
