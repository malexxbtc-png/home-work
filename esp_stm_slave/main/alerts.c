#include "alerts.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    char name[16];
    bool state;
} hyst_item_t;

static hyst_item_t hyst_states[10];
static int hyst_count = 0;

double calculate_dewpoint(double t, double rh) {
    rh = fmax(fmin(rh, 100.0), 0.01);
    double b = 17.62, c = 243.12;
    double g = log(rh / 100.0) + (b * t) / (c + t);
    return c * g / (b - g);
}

static bool check_hyst(const char *name, float value, float on, float off) {
    int idx = -1;
    for (int i = 0; i < hyst_count; i++) {
        if (strcmp(hyst_states[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    bool active = false;
    if (idx >= 0) {
        active = hyst_states[idx].state;
        active = (value >= off) ? (value >= off && active) : (value >= on); 
        if (value >= on) active = true;
        if (value < off) active = false;
        hyst_states[idx].state = active;
    } else if (hyst_count < 10) {
        active = (value >= on);
        strcpy(hyst_states[hyst_count].name, name);
        hyst_states[hyst_count].state = active;
        hyst_count++;
    }
    return active;
}

void alert_manager_init(void) {
    hyst_count = 0;
}

void check_alerts(float t, float rh, float td, float tilt, float shock, bool wx_valid, float wind_w, float wind_g, float rain, float snow, char *out_buf, size_t max_len) {
    out_buf[0] = '\0';
    char temp[128] = "";

    if (wx_valid) {
        bool wind_a = check_hyst("wind_avg", wind_w, WIND_AVG_ALERT, WIND_AVG_ALERT - WIND_HYST);
        bool wind_g_al = check_hyst("wind_gust", wind_g, WIND_GUST_ALERT, WIND_GUST_ALERT - WIND_HYST);
        if (wind_a || wind_g_al) strcat(temp, "wind,");

        if (snow >= SNOW_ALERT_CM) strcat(temp, "snow,");
        else if (rain >= RAIN_ALERT_MM) strcat(temp, "rain,");
    }

    if (td == -32768.0 && t >= -100 && rh >= 0) {
        td = calculate_dewpoint(t, rh);
    }

    if (t > -100 && rh >= 0) {
        bool humid = check_hyst("fog_rh", rh, FOG_RH_ON, FOG_RH_OFF);
        if (humid && (t - td) < FOG_DEW_SPREAD) {
            strcat(temp, "fog,");
        }
    }

    if (t >= ICE_T_MIN && t <= ICE_T_MAX) {
        bool wet = (rh >= ICE_RH) || rain >= RAIN_ALERT_MM || snow >= SNOW_ALERT_CM;
        if (wet) strcat(temp, "ice,");
    }

    if (tilt > TILT_LIMIT_DEG) strcat(temp, "tilt,");
    if (shock > SHOCK_THRESHOLD) strcat(temp, "shock,");

    // Видаляємо останню кому за наявності
    size_t len = strlen(temp);
    if (len > 0 && temp[len - 1] == ',') {
        temp[len - 1] = '\0';
    }
    strncpy(out_buf, temp, max_len);
}