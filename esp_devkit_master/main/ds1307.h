#ifndef DS1307_H
#define DS1307_H

#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t ds1307_init(i2c_master_bus_handle_t bus, uint16_t address);
esp_err_t ds1307_read_time(struct tm *time_info);
esp_err_t ds1307_set_time(const struct tm *time_info);

#endif
