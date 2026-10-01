#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

void leds_init(void);
void led_set_level(uint8_t led_index, uint8_t level);
uint8_t led_get_level(uint8_t led_index);

#endif // LEDS_H