#include "bmp280.h"

#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define REG_ID        0xD0
#define REG_RESET     0xE0
#define REG_CTRL_HUM  0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG    0xF5
#define REG_DATA      0xF7
#define REG_CALIB_TP  0x88
#define REG_CALIB_H1  0xA1
#define REG_CALIB_H2  0xE1

static const char *TAG = "bmp280";

typedef struct {
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1; int16_t H2; uint8_t H3; int16_t H4, H5; int8_t H6;
} calib_t;

static i2c_master_dev_handle_t s_dev;
static calib_t s_cal;
static bool s_ready;
static bool s_bme;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t data[2] = {reg, val};
    return i2c_master_transmit(s_dev, data, sizeof(data), 100);
}

esp_err_t bmp280_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_add(CONFIG_WS_SENSOR_ADDR, 400000, &s_dev), TAG, "add device");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_ID, &id, 1), TAG, "no response (check wiring/address)");
    if (id == 0x60) {
        s_bme = true;
    } else if (id < 0x56 || id > 0x58) {
        ESP_LOGE(TAG, "unexpected chip id 0x%02X", id);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(reg_write(REG_RESET, 0xB6), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t c[24];
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_TP, c, sizeof(c)), TAG, "calib T/P");
    s_cal.T1 = (uint16_t)(c[1] << 8 | c[0]);
    s_cal.T2 = (int16_t)(c[3] << 8 | c[2]);
    s_cal.T3 = (int16_t)(c[5] << 8 | c[4]);
    s_cal.P1 = (uint16_t)(c[7] << 8 | c[6]);
    s_cal.P2 = (int16_t)(c[9] << 8 | c[8]);
    s_cal.P3 = (int16_t)(c[11] << 8 | c[10]);
    s_cal.P4 = (int16_t)(c[13] << 8 | c[12]);
    s_cal.P5 = (int16_t)(c[15] << 8 | c[14]);
    s_cal.P6 = (int16_t)(c[17] << 8 | c[16]);
    s_cal.P7 = (int16_t)(c[19] << 8 | c[18]);
    s_cal.P8 = (int16_t)(c[21] << 8 | c[20]);
    s_cal.P9 = (int16_t)(c[23] << 8 | c[22]);

    if (s_bme) {
        uint8_t h[7];
        ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_H1, &s_cal.H1, 1), TAG, "calib H1");
        ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_H2, h, sizeof(h)), TAG, "calib H");
        s_cal.H2 = (int16_t)(h[1] << 8 | h[0]);
        s_cal.H3 = h[2];
        s_cal.H4 = (int16_t)((int16_t)(int8_t)h[3] * 16 | (h[4] & 0x0F));
        s_cal.H5 = (int16_t)((int16_t)(int8_t)h[5] * 16 | (h[4] >> 4));
        s_cal.H6 = (int8_t)h[6];
    }

    ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG, 0x00), TAG, "config");   /* filter off (forced mode) */
    s_ready = true;
    ESP_LOGI(TAG, "%s detected (id 0x%02X)", s_bme ? "BME280" : "BMP280", id);
    return ESP_OK;
}

bool bmp280_has_humidity(void)
{
    return s_ready && s_bme;
}

esp_err_t bmp280_read(float *temp_c, float *humidity, float *pressure_hpa)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_bme) {
        ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_HUM, 0x01), TAG, "ctrl_hum");   /* humidity x1 */
    }
    /* Forced mode: temp x2, pressure x16 (the chip goes back to sleep by itself). */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_MEAS, (2 << 5) | (5 << 2) | 1), TAG, "trigger");
    vTaskDelay(pdMS_TO_TICKS(50));                                              /* worst case ~43 ms */

    uint8_t d[8];
    ESP_RETURN_ON_ERROR(reg_read(REG_DATA, d, s_bme ? 8 : 6), TAG, "data");
    int32_t adc_p = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_t = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);

    /* Compensation formulas (double version from the Bosch datasheet). */
    double v1 = ((adc_t / 16384.0) - (s_cal.T1 / 1024.0)) * s_cal.T2;
    double v2 = ((adc_t / 131072.0 - s_cal.T1 / 8192.0) *
                 (adc_t / 131072.0 - s_cal.T1 / 8192.0)) * s_cal.T3;
    double t_fine = v1 + v2;
    double temp = t_fine / 5120.0;

    v1 = t_fine / 2.0 - 64000.0;
    v2 = v1 * v1 * s_cal.P6 / 32768.0;
    v2 = v2 + v1 * s_cal.P5 * 2.0;
    v2 = v2 / 4.0 + s_cal.P4 * 65536.0;
    v1 = (s_cal.P3 * v1 * v1 / 524288.0 + s_cal.P2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * s_cal.P1;
    if (v1 == 0.0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    double p = 1048576.0 - adc_p;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = s_cal.P9 * p * p / 2147483648.0;
    v2 = p * s_cal.P8 / 32768.0;
    p = p + (v1 + v2 + s_cal.P7) / 16.0;                 /* Pa */

    double hum = NAN;
    if (s_bme) {
        int32_t adc_h = ((int32_t)d[6] << 8) | d[7];
        double h = t_fine - 76800.0;
        h = (adc_h - (s_cal.H4 * 64.0 + s_cal.H5 / 16384.0 * h)) *
            (s_cal.H2 / 65536.0 * (1.0 + s_cal.H6 / 67108864.0 * h *
                                          (1.0 + s_cal.H3 / 67108864.0 * h)));
        h = h * (1.0 - s_cal.H1 * h / 524288.0);
        hum = h < 0.0 ? 0.0 : (h > 100.0 ? 100.0 : h);
    }

    if (temp < -40.0 || temp > 85.0 || p < 30000.0 || p > 110000.0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *temp_c = (float)temp;
    *pressure_hpa = (float)(p / 100.0);
    if (humidity) {
        *humidity = (float)hum;
    }
    return ESP_OK;
}
