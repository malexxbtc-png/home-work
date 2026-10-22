#ifndef ALERTS_H
#define ALERTS_H

#include <stdbool.h>
#include <stddef.h>

double calculate_dewpoint(double t, double rh);
void alert_manager_init(void);
void check_alerts(float t, float rh, float td, float tilt, float shock, bool wx_valid, float wind_w, float wind_g, float rain, float snow, char *out_buf, size_t max_len);

#endif