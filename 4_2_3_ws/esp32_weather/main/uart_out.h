#pragma once

#include <stdbool.h>
#include "app_types.h"
#include "esp_err.h"

/* UART2 link to the ESP32-S3 (115200 8N1 by default). */
esp_err_t uart_out_init(void);

/* "$SENS,<temp C>,<hum %|NA>,<press hPa>*<XOR hex>\r\n" */
void uart_out_sensors(float temp_c, bool has_humidity, float humidity, float pressure_hpa);

/* Optional (CONFIG_WS_UART_EXTRA): "$GEO,lat,lon,CITY*XX" and "$FCST,idx,date,tmin,tmax,code*XX". */
void uart_out_extra(const station_state_t *s);
