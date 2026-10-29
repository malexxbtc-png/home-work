#ifndef FORECAST_H
#define FORECAST_H

#include <stdbool.h>
#include <stdint.h>

#define FORECAST_MAX_DAYS 7

/* Назва компонента збережена, але Wi-Fi тут немає: погоду з Open-Meteo і час SNTP
   отримує майстер (ESP32 DevKit) і надсилає по UART повідомленнями "wx" і "time". */

typedef struct {
  float temperature_min;
  float temperature_max;
  float precipitation_probability_max;    /* % */
  float wind_speed_max;                   /* м/с */
  int weather_code;                       /* WMO */
} forecast_day_t;

typedef struct {
  float temp;                             /* °C */
  float humidity;                         /* %, NAN якщо немає */
  bool humidity_valid;
  float precipitation;                    /* мм */
  float snowfall;                         /* см */
  int weather_code;
  int cloud_cover;                        /* % */
  float wind_speed;                       /* м/с */
  float wind_gusts;                       /* м/с */
  float wind_direction;                   /* градуси */
  int64_t fetched_at_us;                  /* esp_timer на момент запиту до Open-Meteo (може бути < 0 відразу після старту) */
  bool is_stale;                          /* дані старші за налаштований поріг */
  uint8_t forecast_days;
  forecast_day_t daily[FORECAST_MAX_DAYS];
} current_wx_t;

/* Створює м'ютекс, задає часову зону UTC: системний годинник вузла = місцевий час від майстра */
void forecast_init(void);

/* Розбирає повідомлення {"t":"wx",...} від майстра. true, якщо дані прийняті */
bool forecast_update_from_json(const char *json);

/* Ставить системний годинник за повідомленням {"t":"time","epoch":N}: N = місцевий Unix-час */
void forecast_set_local_time(int64_t local_epoch);

/* Копія останніх даних. false, якщо даних ще немає */
bool forecast_get_current(current_wx_t *weather);

#endif /* FORECAST_H */
