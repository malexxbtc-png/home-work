#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Кермо: 28BYJ-48 через ULN2003 (4 входи), half-step.
 * Позиція рахується від точки старту (при вмиканні = центр). */
typedef struct {
    int gpio[4];            /* IN1..IN4 */
    int halfsteps_per_rev;  /* для 28BYJ-48 з редуктором 1:64 = 4096 */
    int max_angle_deg;      /* ліміт ±max_angle_deg */
    int step_delay_ms;      /* пауза між півкроками, 2 мс зазвичай */
} steering_config_t;

esp_err_t steering_init(const steering_config_t *cfg);
esp_err_t steering_set_angle(int deg);
int       steering_get_angle(void);

/* true, поки кермо крутиться до цілі */
bool      steering_is_moving(void);
