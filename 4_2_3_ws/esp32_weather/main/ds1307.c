#include "ds1307.h"

#include "esp_check.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "timeutil.h"

static const char *TAG = "ds1307";
static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t ds1307_init(void)
{
    /* The DS1307 is limited to 100 kHz. */
    ESP_RETURN_ON_ERROR(i2c_bus_add(CONFIG_WS_RTC_ADDR, 100000, &s_dev), TAG, "add device");
    uint8_t reg = 0x00, sec;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, &sec, 1, 100), TAG,
                        "no response (needs 5 V, check pull-ups)");
    return ESP_OK;
}

esp_err_t ds1307_read_utc(time_t *out)
{
    uint8_t reg = 0x00, b[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof(b), 100), TAG, "read");

    if (b[0] & 0x80) {                  /* CH bit: oscillator halted, time was never set */
        return ESP_ERR_INVALID_STATE;
    }
    struct tm t = {0};
    t.tm_sec  = bcd2bin(b[0] & 0x7F);
    t.tm_min  = bcd2bin(b[1] & 0x7F);
    if (b[2] & 0x40) {                  /* 12-hour mode */
        int h = bcd2bin(b[2] & 0x1F) % 12;
        t.tm_hour = (b[2] & 0x20) ? h + 12 : h;
    } else {
        t.tm_hour = bcd2bin(b[2] & 0x3F);
    }
    t.tm_mday = bcd2bin(b[4] & 0x3F);
    t.tm_mon  = bcd2bin(b[5] & 0x1F) - 1;
    t.tm_year = 100 + bcd2bin(b[6]);

    if (t.tm_year < 124 || t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 ||
        t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 59) {
        return ESP_ERR_INVALID_RESPONSE;    /* year < 2024 or garbage */
    }
    *out = tm_to_utc(&t);
    return ESP_OK;
}

esp_err_t ds1307_write_utc(time_t utc)
{
    struct tm t;
    gmtime_r(&utc, &t);
    const uint8_t buf[8] = {
        0x00,                            /* start register; CH bit = 0 starts the oscillator */
        bin2bcd(t.tm_sec),
        bin2bcd(t.tm_min),
        bin2bcd(t.tm_hour),              /* 24-hour mode */
        (uint8_t)(t.tm_wday + 1),
        bin2bcd(t.tm_mday),
        bin2bcd(t.tm_mon + 1),
        bin2bcd(t.tm_year % 100),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}
