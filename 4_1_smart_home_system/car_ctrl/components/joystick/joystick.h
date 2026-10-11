#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Аналоговий джойстик (2 осі + кнопка). Осі — тільки ADC1 (GPIO1..GPIO10 на ESP32-S3). */
typedef struct {
    int  x_gpio;      /* вісь X */
    int  y_gpio;      /* вісь Y */
    int  sw_gpio;     /* кнопка, -1 якщо немає */
    int  deadzone;    /* мертва зона, сирі одиниці 0..4095 */
    bool invert_y;
} joystick_config_t;

typedef struct {
    int  x;             /* -100..100 */
    int  y;             /* -100..100 */
    bool pressed_edge;  /* true один раз при натисканні кнопки */
} joystick_state_t;

/* Джойстик має стояти у центрі під час старту: центр міряється один раз. */
esp_err_t joystick_init(const joystick_config_t *cfg);
esp_err_t joystick_read(joystick_state_t *out);
