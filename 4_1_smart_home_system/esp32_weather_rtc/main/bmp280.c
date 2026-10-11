#include <string.h>
#include "bmp280.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bmp280";

#define REG_ID        0xD0
#define REG_RESET     0xE0
#define REG_CTRL_HUM  0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG    0xF5
#define REG_DATA      0xF7
#define REG_CALIB_A   0x88   /* 24 байти T1..P9 */
#define REG_CALIB_H1  0xA1
#define REG_CALIB_H2  0xE1   /* 7 байт H2..H6 */

#define CHIP_ID_BMP280  0x58
#define CHIP_ID_BME280  0x60

static i2c_master_dev_handle_t s_dev;
static bool s_has_hum;
static int32_t s_t_fine;

/* калібрувальні коефіцієнти */
static uint16_t T1; static int16_t T2, T3;
static uint16_t P1; static int16_t P2, P3, P4, P5, P6, P7, P8, P9;
static uint8_t H1, H3; static int16_t H2, H4, H5; static int8_t H6;

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t d[2] = { reg, val };
    return i2c_master_transmit(s_dev, d, 2, 100);
}

static esp_err_t try_addr(i2c_master_bus_handle_t bus, uint8_t addr, uint8_t *chip_id)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    err = read_regs(REG_ID, chip_id, 1);
    if (err != ESP_OK || (*chip_id != CHIP_ID_BMP280 && *chip_id != CHIP_ID_BME280)) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return (err != ESP_OK) ? err : ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    uint8_t id = 0;
    esp_err_t err = try_addr(bus, addr, &id);
    if (err != ESP_OK) {
        uint8_t other = (addr == 0x76) ? 0x77 : 0x76;
        ESP_LOGW(TAG, "no sensor at 0x%02X, trying 0x%02X", addr, other);
        err = try_addr(bus, other, &id);
        if (err != ESP_OK) {
            return err;
        }
        addr = other;
    }

    s_has_hum = (id == CHIP_ID_BME280);
    ESP_LOGI(TAG, "found %s at 0x%02X", s_has_hum ? "BME280" : "BMP280", addr);

    /* soft reset і пауза */
    write_reg(REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* калібровка */
    uint8_t c[24];
    err = read_regs(REG_CALIB_A, c, sizeof(c));
    if (err != ESP_OK) {
        return err;
    }
    T1 = (uint16_t)(c[1] << 8 | c[0]);  T2 = (int16_t)(c[3] << 8 | c[2]);  T3 = (int16_t)(c[5] << 8 | c[4]);
    P1 = (uint16_t)(c[7] << 8 | c[6]);  P2 = (int16_t)(c[9] << 8 | c[8]);  P3 = (int16_t)(c[11] << 8 | c[10]);
    P4 = (int16_t)(c[13] << 8 | c[12]); P5 = (int16_t)(c[15] << 8 | c[14]); P6 = (int16_t)(c[17] << 8 | c[16]);
    P7 = (int16_t)(c[19] << 8 | c[18]); P8 = (int16_t)(c[21] << 8 | c[20]); P9 = (int16_t)(c[23] << 8 | c[22]);

    if (s_has_hum) {
        uint8_t h[7];
        err = read_regs(REG_CALIB_H1, &H1, 1);
        if (err != ESP_OK) return err;
        err = read_regs(REG_CALIB_H2, h, sizeof(h));
        if (err != ESP_OK) return err;
        H2 = (int16_t)(h[1] << 8 | h[0]);
        H3 = h[2];
        H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
        H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
        H6 = (int8_t)h[6];

        write_reg(REG_CTRL_HUM, 0x01);       /* osrs_h x1 (до ctrl_meas!) */
    }

    write_reg(REG_CONFIG, 0x90);              /* standby 500 мс, фільтр x16 */
    write_reg(REG_CTRL_MEAS, 0x57);           /* temp x2, press x16, normal mode */
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

bool bmp280_has_humidity(void)
{
    return s_has_hum;
}

/* --- формули компенсації з даташита Bosch --- */

static int32_t comp_temp(int32_t adc_T)  /* 0.01 °C */
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)T1 << 1))) * ((int32_t)T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)T1)) * ((adc_T >> 4) - ((int32_t)T1))) >> 12) * ((int32_t)T3)) >> 14;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8;
}

static uint32_t comp_press(int32_t adc_P)  /* Па в форматі Q24.8 */
{
    int64_t var1 = (int64_t)s_t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)P6;
    var2 = var2 + ((var1 * (int64_t)P5) << 17);
    var2 = var2 + (((int64_t)P4) << 35);
    var1 = ((var1 * var1 * (int64_t)P3) >> 8) + ((var1 * (int64_t)P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)P1) >> 33;
    if (var1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)P7) << 4);
    return (uint32_t)p;
}

static uint32_t comp_hum(int32_t adc_H)  /* %RH в форматі Q22.10 */
{
    int32_t v = s_t_fine - 76800;
    v = (((((adc_H << 14) - (((int32_t)H4) << 20) - (((int32_t)H5) * v)) + 16384) >> 15) *
         (((((((v * ((int32_t)H6)) >> 10) * (((v * ((int32_t)H3)) >> 11) + 32768)) >> 10) + 2097152) *
           ((int32_t)H2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)H1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

esp_err_t bmp280_read(bmp280_data_t *out)
{
    uint8_t d[8] = {0};
    esp_err_t err = read_regs(REG_DATA, d, s_has_hum ? 8 : 6);
    if (err != ESP_OK) {
        return err;
    }

    int32_t adc_P = (int32_t)((uint32_t)d[0] << 12 | (uint32_t)d[1] << 4 | d[2] >> 4);
    int32_t adc_T = (int32_t)((uint32_t)d[3] << 12 | (uint32_t)d[4] << 4 | d[5] >> 4);

    /* 0x80000 на початку = вимірювання ще не готове */
    if (adc_T == 0x80000 || adc_P == 0x80000) {
        return ESP_ERR_INVALID_STATE;
    }

    out->temperature = comp_temp(adc_T) / 100.0f;    /* спочатку T: оновлює t_fine */
    out->pressure = comp_press(adc_P) / 256.0f / 100.0f;   /* Па -> гПа */
    out->has_humidity = s_has_hum;
    if (s_has_hum) {
        int32_t adc_H = (int32_t)((uint32_t)d[6] << 8 | d[7]);
        out->humidity = comp_hum(adc_H) / 1024.0f;
    } else {
        out->humidity = 0;
    }
    return ESP_OK;
}
