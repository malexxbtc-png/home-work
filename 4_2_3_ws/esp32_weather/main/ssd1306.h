#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define OLED_W 128
#define OLED_H 64

esp_err_t ssd1306_init(void);
void      ssd1306_clear(void);

/* 5x7 font, integer scale. (x, y) is the top-left corner. */
void ssd1306_text(int x, int y, int scale, const char *s);
void ssd1306_text_center(int y, int scale, const char *s);
void ssd1306_text_right(int y, int scale, const char *s);
int  ssd1306_text_width(int scale, const char *s);
void ssd1306_hline(int x, int y, int w);

esp_err_t ssd1306_flush(void);
void      ssd1306_power(bool on);
