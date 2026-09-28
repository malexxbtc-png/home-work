#pragma once
#include "driver/gpio.h"

void sound_dac_init(gpio_num_t gpio_num);
void sound_dac_set_tone(uint16_t freq);
void sound_dac_stop(void);