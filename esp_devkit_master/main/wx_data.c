#include "wx_data.h"

#include <math.h>
#include <string.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static wx_view_t cache;
static portMUX_TYPE cache_mux = portMUX_INITIALIZER_UNLOCKED;

static float num(const cJSON *obj, const char *key, float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

bool wx_parse(const char *json, wx_view_t *out)
{
    if (json == NULL || out == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cur");
    if (!cJSON_IsObject(cur)) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->wind = num(cur, "w", 0);
    out->gust = num(cur, "g", 0);
    out->dir = num(cur, "dir", 0);
    out->rain = num(cur, "rain", 0);
    out->snow = num(cur, "snow", 0);
    out->rh = num(cur, "RH", NAN);
    out->cloud = (int)num(cur, "cloud", 0);

    const cJSON *fc = cJSON_GetObjectItemCaseSensitive(root, "fc");
    if (cJSON_IsArray(fc)) {
        const cJSON *d;
        cJSON_ArrayForEach(d, fc) {
            if (out->n_days >= WX_DAYS) {
                break;
            }
            int i = out->n_days++;
            out->day[i].cond = (int)num(d, "cond", 0);
            out->day[i].tmin = num(d, "tmin", NAN);
            out->day[i].tmax = num(d, "tmax", NAN);
            out->day[i].pop = num(d, "pop", 0);
            out->day[i].wmax = num(d, "wmax", 0);
        }
    }
    out->valid = true;
    cJSON_Delete(root);
    return true;
}

void wx_cache_set(const wx_view_t *wx)
{
    portENTER_CRITICAL(&cache_mux);
    cache = *wx;
    portEXIT_CRITICAL(&cache_mux);
}

bool wx_cache_get(wx_view_t *out)
{
    bool ok;
    portENTER_CRITICAL(&cache_mux);
    *out = cache;
    ok = cache.valid;
    portEXIT_CRITICAL(&cache_mux);
    return ok;
}

const char *wx_cond_text(int wmo)
{
    switch (wmo) {
    case 0:  return "Clear";
    case 1:  return "Mostly clr";
    case 2:  return "Part cloud";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Frz drizl";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Frz rain";
    case 71: return "Lt snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grain";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow shwr";
    case 95: return "Thunder";
    case 96: case 99: return "Thund+hail";
    default: return "?";
    }
}

const char *wind_dir_text(float deg)
{
    static const char *const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (!isfinite(deg)) {
        return "-";
    }
    int idx = (int)floorf((deg + 22.5f) / 45.0f) % 8;
    if (idx < 0) idx += 8;
    return names[idx];
}
