#pragma once

#include <stddef.h>
#include "app_types.h"
#include "esp_err.h"

/* Open-Meteo: 3-day daily forecast for the coordinates. */
esp_err_t weather_fetch_forecast(double lat, double lon, forecast_day_t out[FORECAST_DAYS]);

/* BigDataCloud reverse geocoding: upper-case ASCII city name. */
esp_err_t weather_fetch_city(double lat, double lon, char *out, size_t out_len);

/* Short ASCII description for a WMO weather code. */
const char *weather_wmo_text(int code);
