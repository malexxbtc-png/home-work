#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// Конфігурація вибраних пінів
#define LED_GPIO            GPIO_NUM_5       // Вихід на LED (GPIO5)
#define ADC_CHANNEL_SELECT  ADC_CHANNEL_0    // Вхід АЦП на GPIO4 (ADC2_CH0 / ADC1_CH3 в залежності від ревізії)

// Параметри обробки сигналу
#define SMA_WINDOW_SIZE     10
#define THRESHOLD_DARK      2500
#define THRESHOLD_LIGHT     1800
#define SAMPLE_PERIOD_MS    100