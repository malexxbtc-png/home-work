#pragma once
#include "driver/gpio.h"

#define LED1_GPIO GPIO_NUM_23
#define LED2_GPIO GPIO_NUM_22

void led_effects_init(void);
void led_effects_update(bool is_note_playing);