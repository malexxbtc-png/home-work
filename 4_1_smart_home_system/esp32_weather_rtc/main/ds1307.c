#include <string.h>
#include "ds1307.h"
#include "esp_log.h"

static const char *TAG = "ds1307";

#define REG_SECONDS 0x00
#define CH_BIT      0x80   /* clock halt: 1 = генератор зупинений */
#define HOUR_12H    0x40

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static bool bcd_ok(uint8_t v)     { return (v & 0x0F) <= 9 && (v >> 4) <= 9; }

/* UTC struct tm -> time_t без залежності від TZ (алгоритм days_from_civil) */
static time_t utc_to_time(int year, int mon, int day, int hour, int min, int sec)
{
    int y = year - (mon <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(mon + (mon > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400L + hour * 3600L + min * 60L + sec);
}

esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 50000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t reg = REG_SECONDS, sec;
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &sec, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no answer at 0x%02X (%s). Модулю потрібне живлення 5V!",
                 addr, esp_err_to_name(err));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err;
    }
    ESP_LOGI(TAG, "found at 0x%02X, oscillator %s", addr, (sec & CH_BIT) ? "HALTED" : "running");
    return ESP_OK;
}

esp_err_t ds1307_read_utc(time_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = REG_SECONDS, r[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) {
        return err;
    }
    if (r[0] & CH_BIT) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t s = r[0] & 0x7F, mi = r[1], h = r[2], d = r[4], mo = r[5], y = r[6];
    if (!bcd_ok(s) || !bcd_ok(mi) || !bcd_ok(d) || !bcd_ok(mo) || !bcd_ok(y)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int hour;
    if (h & HOUR_12H) {                       /* 12-годинний режим (bit5 = PM) */
        uint8_t hour_12 = h & 0x1F;
        if (!bcd_ok(hour_12) || bcd2bin(hour_12) < 1 || bcd2bin(hour_12) > 12) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        hour = bcd2bin(hour_12) % 12;
        if (h & 0x20) {
            hour += 12;
        }
    } else {
        uint8_t hour_24 = h & 0x3F;
        if (!bcd_ok(hour_24) || bcd2bin(hour_24) > 23) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        hour = bcd2bin(hour_24);
    }
    int sec = bcd2bin(s), min = bcd2bin(mi), day = bcd2bin(d), mon = bcd2bin(mo), year = 2000 + bcd2bin(y);
    if (sec > 59 || min > 59 || day < 1 || day > 31 || mon < 1 || mon > 12) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    static const uint8_t days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint8_t max_day = days_per_month[mon - 1];
    if (mon == 2 && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0)) {
        max_day++;
    }
    if (day > max_day) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out = utc_to_time(year, mon, day, hour, min, sec);
    return ESP_OK;
}

esp_err_t ds1307_write_utc(time_t t)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    struct tm tm;
    gmtime_r(&t, &tm);
    uint8_t buf[8] = {
        REG_SECONDS,
        bin2bcd((uint8_t)tm.tm_sec),            /* CH = 0 -> генератор іде */
        bin2bcd((uint8_t)tm.tm_min),
        bin2bcd((uint8_t)tm.tm_hour),           /* 24-годинний режим */
        bin2bcd((uint8_t)(tm.tm_wday + 1)),
        bin2bcd((uint8_t)tm.tm_mday),
        bin2bcd((uint8_t)(tm.tm_mon + 1)),
        bin2bcd((uint8_t)((tm.tm_year + 1900) % 100)),
    };
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC set (UTC) %04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    return err;
}
