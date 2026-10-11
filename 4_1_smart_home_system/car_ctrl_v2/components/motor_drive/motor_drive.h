#pragma once
#include "esp_err.h"

/* Драйвер L298N для одного колекторного мотора. */
typedef struct {
    int ena_gpio;        /* PWM */
    int in1_gpio;        /* -1: режим одного MOSFET, тільки вперед (in2 не використовується) */
    int in2_gpio;        /* -1 разом з in1 */
    int pwm_freq_hz;
    int min_duty_pct;    /* 0..99: мінімальна шпаруватість при ненульовій тязі (старт мотора) */
    int max_duty_pct;    /* min+1..100: шпаруватість при повній тязі (обмеження напруги на моторі) */
} motor_drive_config_t;

esp_err_t motor_drive_init(const motor_drive_config_t *cfg);

/* percent: -100 (назад) .. 100 (вперед), 0 = стоп.
 * Ненульові значення відображаються на діапазон [min_duty_pct .. max_duty_pct]. */
esp_err_t motor_drive_set_percent(int percent);
int       motor_drive_get_percent(void);
