#ifndef ALERTS_H
#define ALERTS_H

#include <stdbool.h>
#include <stddef.h>

double calculate_dewpoint(double t, double rh);
void alert_manager_init(void);

/* Формує список активних тривог через кому ("wind,fog"). Дані погоди (вітер, дощ, сніг) беруться з
   Open-Meteo (Wi-Fi на майстрі) і враховуються лише коли wx_valid = true.
   Температуру < -100 вважаємо «немає даних». rh = NAN, якщо датчик без вологості. */
void check_alerts(float t, float rh, float td,
                  bool wx_valid, float wind_avg, float wind_gust, float rain, float snow,
                  char *out_buf, size_t max_len);

#endif
