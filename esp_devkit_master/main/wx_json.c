#include "wx_json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static bool get_number(const cJSON *object, const char *key, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

static bool get_array_number(const cJSON *object, const char *key, int index, double *value)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    const cJSON *item = cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

bool wx_json_parse_open_meteo(const char *body, net_wx_t *out)
{
    if (body == NULL || out == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return false;
    }
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    net_wx_t w;
    memset(&w, 0, sizeof(w));
    double v;

    if (!cJSON_IsObject(current) || !get_number(current, "temperature_2m", &v)) {
        cJSON_Delete(root);
        return false;
    }
    w.temp = (float)v;
    w.rh = get_number(current, "relative_humidity_2m", &v) ? (float)v : NAN;
    if (!get_number(current, "precipitation", &v)) goto fail;
    w.rain = (float)v;
    if (!get_number(current, "snowfall", &v)) goto fail;
    w.snow = (float)v;
    if (!get_number(current, "weather_code", &v)) goto fail;
    w.code = (int)v;
    if (!get_number(current, "cloud_cover", &v)) goto fail;
    w.cloud = (int)v;
    if (!get_number(current, "wind_speed_10m", &v)) goto fail;
    w.wind = (float)v;
    if (!get_number(current, "wind_gusts_10m", &v)) goto fail;
    w.gust = (float)v;
    if (!get_number(current, "wind_direction_10m", &v)) goto fail;
    w.dir = (float)v;

    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const cJSON *dates = daily != NULL ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
    if (cJSON_IsArray(dates)) {
        int count = cJSON_GetArraySize(dates);
        if (count > NETWX_MAX_DAYS) {
            count = NETWX_MAX_DAYS;
        }
        for (int i = 0; i < count; i++) {
            double tmin, tmax, pop, wmax, cond;
            if (!get_array_number(daily, "temperature_2m_min", i, &tmin) ||
                !get_array_number(daily, "temperature_2m_max", i, &tmax) ||
                !get_array_number(daily, "precipitation_probability_max", i, &pop) ||
                !get_array_number(daily, "wind_speed_10m_max", i, &wmax) ||
                !get_array_number(daily, "weather_code", i, &cond)) {
                continue;
            }
            int n = w.n_days++;
            w.day[n].cond = (int)cond;
            w.day[n].tmin = (float)tmin;
            w.day[n].tmax = (float)tmax;
            w.day[n].pop = (float)pop;
            w.day[n].wmax = (float)wmax;
        }
    }
    cJSON_Delete(root);
    *out = w;
    return true;

fail:
    cJSON_Delete(root);
    return false;
}

static double r1(double value)
{
    return round(value * 10.0) / 10.0;
}

int wx_json_build(const net_wx_t *w, int age_s, char *dest, size_t size)
{
    if (w == NULL || dest == NULL || size == 0) {
        return -1;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *cur = cJSON_CreateObject();
    cJSON *days = cJSON_CreateArray();
    if (root == NULL || cur == NULL || days == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(cur);
        cJSON_Delete(days);
        return -1;
    }
    cJSON_AddStringToObject(root, "t", "wx");
    cJSON_AddNumberToObject(root, "age", age_s < 0 ? 0 : age_s);
    cJSON_AddNumberToObject(cur, "T", r1(w->temp));
    cJSON_AddNumberToObject(cur, "w", r1(w->wind));
    cJSON_AddNumberToObject(cur, "g", r1(w->gust));
    cJSON_AddNumberToObject(cur, "dir", round(w->dir));
    cJSON_AddNumberToObject(cur, "rain", r1(w->rain));
    cJSON_AddNumberToObject(cur, "snow", r1(w->snow));
    if (isfinite(w->rh)) {
        cJSON_AddNumberToObject(cur, "RH", round(w->rh));
    } else {
        cJSON_AddNullToObject(cur, "RH");
    }
    cJSON_AddNumberToObject(cur, "cloud", w->cloud);
    cJSON_AddNumberToObject(cur, "code", w->code);
    cJSON_AddItemToObject(root, "cur", cur);

    for (int i = 0; i < w->n_days && i < NETWX_MAX_DAYS; i++) {
        cJSON *d = cJSON_CreateObject();
        if (d == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(days);
            return -1;
        }
        cJSON_AddNumberToObject(d, "cond", w->day[i].cond);
        cJSON_AddNumberToObject(d, "tmin", r1(w->day[i].tmin));
        cJSON_AddNumberToObject(d, "tmax", r1(w->day[i].tmax));
        cJSON_AddNumberToObject(d, "pop", round(w->day[i].pop));
        cJSON_AddNumberToObject(d, "wmax", r1(w->day[i].wmax));
        cJSON_AddItemToArray(days, d);
    }
    cJSON_AddItemToObject(root, "fc", days);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return -1;
    }
    size_t n = strlen(text);
    int result = -1;
    if (n < size) {
        memcpy(dest, text, n + 1);
        result = (int)n;
    }
    free(text);
    return result;
}
