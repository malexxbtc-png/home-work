#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "driver/i2c_master.h"

/* Ініціалізує DS1307; якщо годинник не заведений, ставить час збірки прошивки */
void clock_init(i2c_master_bus_handle_t bus, uint16_t address);

/* Запускає задачі: читання DS1307 двічі на секунду і корекція за часом SNTP */
void clock_start(void);

/* Кешований час RTC (місцевий). false, якщо RTC недоступний */
bool clock_get(struct tm *out);

/* Мітка часу для подій: "25.10 19:45:12" або "up 01:23:45", якщо RTC недоступний */
void clock_stamp(char *buf, size_t size);

#endif
