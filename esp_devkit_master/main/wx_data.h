#ifndef WX_DATA_H
#define WX_DATA_H

#include <stdbool.h>

#define WX_DAYS 3

typedef struct {
    bool valid;
    float wind, gust, dir;       /* м/с, м/с, градуси */
    float rain, snow;            /* мм, см */
    float rh;                    /* %, NAN якщо немає */
    int cloud;                   /* % */
    int n_days;
    struct {
        int cond;                /* код WMO */
        float tmin, tmax, pop, wmax;
    } day[WX_DAYS];
} wx_view_t;

/* Розбирає повідомлення {"t":"wx","cur":{...},"fc":[...]} (див. wx_json.h) */
bool wx_parse(const char *json, wx_view_t *out);

/* Кеш: пишуть у task_sensors, читають у UI */
void wx_cache_set(const wx_view_t *wx);
bool wx_cache_get(wx_view_t *out);

const char *wx_cond_text(int wmo);       /* до 10 символів */
const char *wind_dir_text(float deg);    /* N, NE, E ... */

#endif
