#pragma once

#include "esp_err.h"

/* Screen cycle on the OLED: clock (4x longer) -> temperature -> humidity (BME280 only)
 * -> pressure -> city/coordinates -> 3-day forecast. Screens without data are skipped. */
esp_err_t ui_start(void);

/* Stops drawing and switches the OLED off (before deep sleep). */
void ui_sleep(void);
