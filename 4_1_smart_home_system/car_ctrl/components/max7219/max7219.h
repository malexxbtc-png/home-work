#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Одна матриця 8x8 на MAX7219 (SPI, без декодування). */
typedef struct {
    int     mosi_gpio;
    int     clk_gpio;
    int     cs_gpio;
    uint8_t intensity;   /* 0..15 */
} max7219_config_t;

esp_err_t max7219_init(const max7219_config_t *cfg);

/* rows[0] — верхній рядок, біт 7 — ліва колонка */
esp_err_t max7219_draw(const uint8_t rows[8]);
