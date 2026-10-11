#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Джойстик -> тяга (пропорційно осі Y) і кермо (за вектором стіка).
 * Якщо джойстика немає, повертає помилку, а керування з UART лишається. */
esp_err_t control_start(void);

/* true, коли джойстик працює і має пріоритет над M:/S: з UART. */
bool control_joystick_active(void);
