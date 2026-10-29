#include "alerts.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { H_WIND_AVG, H_WIND_GUST, H_FOG_RH, H_COUNT };
static bool hyst_state[H_COUNT];

double calculate_dewpoint(double t, double rh)
{
    rh = fmax(fmin(rh, 100.0), 0.01);
    const double b = 17.62, c = 243.12;
    double g = log(rh / 100.0) + (b * t) / (c + t);
    return c * g / (b - g);
}

/* Тригер із гістерезисом: вмикається при value >= on, вимикається при value < off */
static bool hyst(int idx, float value, float on, float off)
{
    if (!isfinite(value)) {
        hyst_state[idx] = false;
        return false;
    }
    if (value >= on) {
        hyst_state[idx] = true;
    } else if (value < off) {
        hyst_state[idx] = false;
    }
    return hyst_state[idx];
}

void alert_manager_init(void)
{
    memset(hyst_state, 0, sizeof(hyst_state));
}

static void add(char *buf, size_t size, const char *name)
{
    size_t len = strlen(buf);
    if (len + strlen(name) + 2 > size) {
        return;
    }
    snprintf(buf + len, size - len, "%s%s", len ? "," : "", name);
}

void check_alerts(float t, float rh, float td,
                  bool wx_valid, float wind_avg, float wind_gust, float rain, float snow,
                  char *out_buf, size_t max_len)
{
    if (out_buf == NULL || max_len == 0) {
        return;
    }
    char tmp[128] = "";
    const bool have_t = isfinite(t) && t > -100.0f;
    const bool have_rh = isfinite(rh) && rh >= 0.0f;

    if (!wx_valid) {
        rain = snow = 0.0f;
        hyst(H_WIND_AVG, NAN, 0, 0);
        hyst(H_WIND_GUST, NAN, 0, 0);
    } else {
        bool wa = hyst(H_WIND_AVG, wind_avg, WIND_AVG_ALERT, WIND_AVG_ALERT - WIND_HYST);
        bool wg = hyst(H_WIND_GUST, wind_gust, WIND_GUST_ALERT, WIND_GUST_ALERT - WIND_HYST);
        if (wa || wg) add(tmp, sizeof(tmp), "wind");
        if (snow >= SNOW_ALERT_CM) add(tmp, sizeof(tmp), "snow");
        else if (rain >= RAIN_ALERT_MM) add(tmp, sizeof(tmp), "rain");
    }

    if (have_t && have_rh) {
        if (!isfinite(td) || td <= -32768.0f + 1) {
            td = (float)calculate_dewpoint(t, rh);
        }
        bool humid = hyst(H_FOG_RH, rh, FOG_RH_ON, FOG_RH_OFF);
        if (humid && (t - td) < FOG_DEW_SPREAD) add(tmp, sizeof(tmp), "fog");
    } else {
        hyst(H_FOG_RH, NAN, 0, 0);
    }

    if (have_t && t >= ICE_T_MIN && t <= ICE_T_MAX) {
        bool wet = (have_rh && rh >= ICE_RH) || rain >= RAIN_ALERT_MM || snow >= SNOW_ALERT_CM;
        if (wet) add(tmp, sizeof(tmp), "ice");
    }

    snprintf(out_buf, max_len, "%s", tmp);
}
