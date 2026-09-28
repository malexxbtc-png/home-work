#pragma once
#include "driver/gpio.h"

void sound_pwm_init(gpio_num_t gpio_num);
void sound_pwm_set_tone(uint16_t freq);
void sound_pwm_stop(void);