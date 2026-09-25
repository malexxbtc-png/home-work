#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

static const char *TAG = "POT_ADC_TEST";

// --- Налаштування АЦП ---
#define ADC_INPUT_CHANNEL   ADC1_CHANNEL_6    // Пін GPIO34 (або інший аналоговий пін)
#define ADC_ATTEN           ADC_ATTEN_DB_12   // Атенюація: діапазон приблизно до 3.3В
#define ADC_WIDTH           ADC_WIDTH_BIT_12  // Розрядність: 12 біт (макс. значення 4095)
#define V_REF_MV            3300.0f           // Опорна напруга (номінальна 3300 мВ)
#define ADC_MAX_CODE        4095.0f           // Максимальний RAW код для 12 біт
#define N_AVG               16                // Кількість вимірів для фільтрації (оверсемплінг)
#define SAMPLE_PERIOD_MS    100               // Період опитування (мс)
// Опорні точки для калібрування (змініть Y_0 та Y_1 у мілівольтах після заміру мультиметром)
#define RAW_0   860
#define Y_0     850.0f   // у мВ (наприклад, 0.85В = 850мВ)
#define RAW_1   2900
#define Y_1     2900.0f
static esp_adc_cal_characteristics_t adc_chars;
static bool cal_enabled = false;

static void setup_adc(void) {
    // 1. Налаштування розрядності
    adc1_config_width(ADC_WIDTH);
    // 2. Налаштування атенюації
    adc1_config_channel_atten(ADC_INPUT_CHANNEL, ADC_ATTEN);

    // 3. Характеристика для калібрування (використовуємо eFuse / Vref)
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        ADC_UNIT_1, 
        ADC_ATTEN, 
        ADC_WIDTH, 
        1100, // Заводське опорне значення за замовчуванням (1100 мВ)
        &adc_chars
    );

    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "Calibrated using eFuse Vref");
        cal_enabled = true;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "Calibrated using Two Point eFuse");
        cal_enabled = true;
    } else {
        ESP_LOGW(TAG, "Using default Vref (No eFuse calibration found)");
        cal_enabled = true; // Все одно використовуємо структуру для розрахунку
    }
}

// Функція читання з усредненням (програмний фільтр від шумів)
static int read_raw_avg(void) {
    uint32_t acc = 0;
    for (int i = 0; i < N_AVG; i++) {
        acc += adc1_get_raw(ADC_INPUT_CHANNEL);
    }
    return (int)(acc / N_AVG);
}

void app_main(void) {
    setup_adc();

    // Вивід інформації про параметри налаштування
    printf("\n==================================================\n");
    printf(" ESP32 ADC Potentiometer Measurement Task\n");
    printf("--------------------------------------------------\n");
    printf(" Resolution : 12-bit (Max code: %.0f)\n", ADC_MAX_CODE);
    printf(" Attenuation: 12 dB (~0 - 3.3V range)\n");
    printf(" V_Ref      : %.0f mV\n", V_REF_MV);
    printf(" Channel    : ADC1_CHANNEL_6 (GPIO34)\n");
    printf("==================================================\n\n");

    // Заголовки таблиці
    printf(" RAW   U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("------------------------------------------\n");

    while (1) {
        // Зчитування RAW зі згладжуванням
        int raw = read_raw_avg();

        // 1. Обчислення напруги за ідеальною формулою: U = (RAW / MAX) * V_REF
        float u_manual = ((float)raw * V_REF_MV) / ADC_MAX_CODE;

        // 2. Зчитування напруги за допомогою апаратного/каліброваного розрахунку ESP-IDF
        uint32_t u_cali_mv = 0;
        if (cal_enabled) {
            u_cali_mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
        } else {
            u_cali_mv = (uint32_t)u_manual;
        }

        // 3. Обчислення похибки (%) між ручним (ідеальним) та каліброваним значенням
        float error_pct = 0.0f;
        if (u_cali_mv > 0) {
            error_pct = (fabsf(u_manual - (float)u_cali_mv) / (float)u_cali_mv) * 100.0f;
        }

        // Вивід рядка таблиці
        printf("%4d     %8.1f     %8lu     %6.2f\n", 
               raw, u_manual, u_cali_mv, error_pct);

        // Пауза ~100 мс
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}