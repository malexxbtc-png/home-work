#ifndef CONFIG_H
#define CONFIG_H

#include "driver/gpio.h"
#include "hal/adc_types.h"

// Апаратні налаштування
#define LED_GPIO            GPIO_NUM_15
#define ADC_UNIT_SELECT     ADC_UNIT_1
#define ADC_CHANNEL_SELECT  ADC_CHANNEL_6  // GPIO34 на ESP32 DevKitC

// Параметри фільтрації та гістерезису
#define SMA_WINDOW_SIZE     10
#define THRESHOLD_DARK      2500  // Поріг: значення вище цього = темно (увімкнути LED)
#define THRESHOLD_LIGHT     1800  // Поріг: значення нижче цього = світло (вимкнути LED)
#define SAMPLE_PERIOD_MS    100

#endif // CONFIG_H