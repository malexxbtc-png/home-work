#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FORECAST_DAYS 3
#define CITY_MAX_LEN  32

typedef struct {
    char  date[11];   /* "YYYY-MM-DD" (local date of the forecast day) */
    float tmax;
    float tmin;
    int   wmo_code;
} forecast_day_t;

typedef struct {
    bool   geo_valid;
    double lat;
    double lon;

    char   city[CITY_MAX_LEN];

    bool           has_forecast;
    time_t         forecast_time;   /* when the forecast was fetched (UTC) */
    forecast_day_t days[FORECAST_DAYS];

    bool  has_sensors;
    float temp_c;
    bool  has_humidity;             /* only BME280 has a humidity sensor */
    float humidity;
    float pressure_hpa;
} station_state_t;

/* Shared state: always take g_state_mtx before touching g_state. */
extern station_state_t   g_state;
extern SemaphoreHandle_t g_state_mtx;
