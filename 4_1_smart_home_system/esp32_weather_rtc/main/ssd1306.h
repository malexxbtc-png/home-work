#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

/** Додає SSD1306 на I2C шину, ініціалізує дисплей. */
esp_err_t ssd1306_init(i2c_master_bus_handle_t bus, uint8_t addr);

/** Очистити буфер (не екран). */
void ssd1306_clear(void);

/** Малює піксель у буфер. */
void ssd1306_pixel(int x, int y, bool on);

/** Малює рядок шрифтом 5x7 з масштабом scale (1..4). Повертає ширину в пікселях. */
int ssd1306_draw_string(int x, int y, const char *s, int scale);

/** Ширина рядка в пікселях. */
int ssd1306_string_width(const char *s, int scale);

/** Рядок по центру екрана по горизонталі. */
void ssd1306_draw_centered(int y, const char *s, int scale);

/** Відправити буфер на дисплей. */
esp_err_t ssd1306_flush(void);
