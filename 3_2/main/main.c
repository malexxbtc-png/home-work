
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define ADC_GPIO          GPIO_NUM_4
#define ADC_BITWIDTH      ADC_BITWIDTH_12
#define ADC_ATTEN_DB      ADC_ATTEN_DB_12
#define SAMPLE_PERIOD_MS  100     // Частота зчитування кожні ~100 мс
#define U_FS_VOLTS        3300.0f // Опорна напруга в мВ (3.3В)
#define ADC_MAX_CODE      4095.0f // Максимальне значення для 12 біт
#define N_AVG             4       // Усереднення для стабільності

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_channel;
static adc_cali_handle_t s_cali_handle = NULL;
static bool s_calibrated = false;

static void adc_calibration_init(void) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali_handle) == ESP_OK) {
        s_calibrated = true;
    }
#endif
}

static void setup_adc(void) {
    adc_unit_t unit = 0;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ADC_GPIO, &unit, &s_channel));
    
    adc_oneshot_unit_init_cfg_t init = { .unit_id = unit };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init, &s_adc));

    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN_DB,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_channel, &ch));

    adc_calibration_init();
}

static int read_raw_avg(void) {
    int acc = 0;
    for (int i = 0; i < N_AVG; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc, s_channel, &raw));
        acc += raw;
    }
    return acc / N_AVG;
}

void app_main(void) {
    setup_adc();

    // Виведення шапки таблиці згідно з вимогами завдання
    printf("\n");
    printf(" RAW    U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("------------------------------------------\n");

    while (1) {
        int raw = read_raw_avg();
        
        // 1. Обчислення ручної напруги (у мВ) за формулою
        float u_manual = ((float)raw * U_FS_VOLTS) / ADC_MAX_CODE;

        // 2. Зчитування напруги за допомогою каліброваного АЦП (у мВ)
        int u_cali_mv = 0;
        if (s_calibrated) {
            adc_cali_raw_to_voltage(s_cali_handle, raw, &u_cali_mv);
        } else {
            u_cali_mv = (int)u_manual;
        }

        // 3. Обчислення похибки (%) між ручною та каліброваною напругою
        float error_pct = 0.0f;
        if (u_cali_mv > 0) {
            error_pct = (fabsf(u_manual - (float)u_cali_mv) / (float)u_cali_mv) * 100.0f;
        }

        // Виведення відформатованого рядка в консоль
        printf("%4d     %8.1f     %8d     %6.2f\n", 
               raw, u_manual, u_cali_mv, error_pct);

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}