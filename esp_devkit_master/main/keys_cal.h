#ifndef KEYS_CAL_H
#define KEYS_CAL_H

#include "ssd1306.h"

/* Якщо при вмиканні утримується кнопка — запускає майстер калібрування на OLED:
   «відпустіть усе» -> по черзі RIGHT, UP, DOWN, LEFT, SELECT. Результат зберігається в NVS.
   Викликати після keys_adc_init() і ssd1306_init(). Повертає true, якщо калібрування виконано. */
bool keys_cal_run_if_requested(ssd1306_handle_t oled);

#endif
