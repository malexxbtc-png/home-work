#ifndef FORECAST_H
#define FORECAST_H

#include <stdbool.h>
#include <stdint.h>

#define FORECAST_MAX_DAYS 7

typedef struct {
  char date[11];                          /* YYYY-MM-DD */
  float temperature_min;
  float temperature_max;
  float precipitation_probability_max;    /* % */
  float wind_speed_max;                   /* м/с */
  int weather_code;                       /* WMO */
} forecast_day_t;

typedef struct {
  float temp;                             /* °C */
  float app_temp;
  float humidity;                         /* %, NAN якщо немає */
  bool humidity_valid;
  float precipitation;                    /* мм */
  float snowfall;                         /* см */
  int weather_code;
  int cloud_cover;                        /* % */
  float pressure;                         /* гПа, на рівні моря */
  float wind_speed;                       /* м/с */
  float wind_gusts;                       /* м/с */
  float wind_direction;                   /* градуси */
  int64_t timestamp;                      /* Unix-час отримання, 0 якщо SNTP ще не синхронізовано */
  int64_t fetched_at_us;                  /* esp_timer на момент отримання */
  bool is_stale;                          /* дані старші за налаштований поріг */
  uint8_t forecast_days;
  forecast_day_t daily[FORECAST_MAX_DAYS];
} current_wx_t;

/* Створює м'ютекс і задає часову зону (час до Wi-Fi вже показується правильно) */
void forecast_init(void);

/* Запускає задачу: Wi-Fi, SNTP, запит до Open-Meteo кожні N хвилин */
void forecast_start_task(void);

/* Копія останніх даних. false, якщо даних ще немає */
bool forecast_get_current(current_wx_t *weather);

#endif /* FORECAST_H */
