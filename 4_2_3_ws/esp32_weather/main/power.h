#pragma once

#include <stdbool.h>

/* Logs the wakeup cause. Call once at boot. */
void power_init(void);

/* Enters deep sleep with the PIR as wakeup source. Returns false (and does not sleep)
 * if the PIR output is currently high, because that would wake the chip immediately. */
bool power_enter_deep_sleep(void);
