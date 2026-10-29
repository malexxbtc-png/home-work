#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

/* Знак градуса в шрифті: "%.1f" DEG_STR "C" (літерали розділені, щоб \x7f не з'їв літеру C) */
#define DEG_STR "\x7f"

typedef struct ssd1306_device *ssd1306_handle_t;

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus, uint16_t address, ssd1306_handle_t *handle);
void ssd1306_clear(ssd1306_handle_t handle, bool color);
void ssd1306_draw_string(ssd1306_handle_t handle, int x, int y, const char *text);
void ssd1306_draw_string_scaled(ssd1306_handle_t handle, int x, int y, const char *text, int scale);
void ssd1306_hline(ssd1306_handle_t handle, int x, int y, int width);
void ssd1306_invert_rect(ssd1306_handle_t handle, int x, int y, int width, int height);
void ssd1306_show(ssd1306_handle_t handle);

#endif
