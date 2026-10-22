#include "weather.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define HTTP_BUF_SIZE 8192

static const char *TAG = "weather";

static esp_err_t http_get(const char *url, char *buf, size_t cap)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        size_t n = 0;
        int r;
        while (n < cap - 1 && (r = esp_http_client_read(c, buf + n, cap - 1 - n)) > 0) {
            n += (size_t)r;
        }
        buf[n] = '\0';
        if (status != 200 || n == 0) {
            ESP_LOGW(TAG, "HTTP status %d, %u bytes", status, (unsigned)n);
            err = ESP_FAIL;
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

/* ---- tiny JSON helpers: the Open-Meteo reply has a fixed, flat shape ---- */

static const char *find_array(const char *json, const char *key)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":[", key);
    const char *p = strstr(json, pat);
    return p ? p + strlen(pat) : NULL;
}

static bool parse_num_array(const char *p, double *out, int n)
{
    for (int i = 0; i < n; i++) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        char *end;
        out[i] = strtod(p, &end);
        if (end == p) {
            return false;        /* "null" or garbage */
        }
        p = end;
    }
    return true;
}

static bool parse_date_array(const char *p, char out[][11], int n)
{
    for (int i = 0; i < n; i++) {
        while (*p && *p != '"') {
            p++;
        }
        if (!*p) {
            return false;
        }
        p++;
        size_t k = 0;
        while (*p && *p != '"' && k < 10) {
            out[i][k++] = *p++;
        }
        out[i][k] = '\0';
        if (*p != '"' || k != 10) {
            return false;
        }
        p++;
    }
    return true;
}

esp_err_t weather_fetch_forecast(double lat, double lon, forecast_day_t out[FORECAST_DAYS])
{
    char *buf = malloc(HTTP_BUF_SIZE);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min"
             "&forecast_days=%d&timezone=auto",
             lat, lon, FORECAST_DAYS);

    esp_err_t err = http_get(url, buf, HTTP_BUF_SIZE);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    /* Everything of interest is inside "daily": {...}; skip the "daily_units" block before it. */
    const char *daily = strstr(buf, "\"daily\":{");
    char dates[FORECAST_DAYS][11];
    double tmax[FORECAST_DAYS], tmin[FORECAST_DAYS], code[FORECAST_DAYS];
    const char *a_time, *a_max, *a_min, *a_code;

    err = ESP_ERR_INVALID_RESPONSE;
    if (daily &&
        (a_time = find_array(daily, "time")) &&
        (a_max  = find_array(daily, "temperature_2m_max")) &&
        (a_min  = find_array(daily, "temperature_2m_min")) &&
        (a_code = find_array(daily, "weather_code")) &&
        parse_date_array(a_time, dates, FORECAST_DAYS) &&
        parse_num_array(a_max, tmax, FORECAST_DAYS) &&
        parse_num_array(a_min, tmin, FORECAST_DAYS) &&
        parse_num_array(a_code, code, FORECAST_DAYS)) {
        for (int i = 0; i < FORECAST_DAYS; i++) {
            memcpy(out[i].date, dates[i], sizeof(out[i].date));
            out[i].tmax = (float)tmax[i];
            out[i].tmin = (float)tmin[i];
            out[i].wmo_code = (int)code[i];
        }
        err = ESP_OK;
    }
    free(buf);
    return err;
}

/* The reverse-geocode reply is large; the city key appears early, so a text search
 * is more robust than parsing a possibly truncated JSON document. */
static bool extract_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = strstr(json, key);
    if (!p) {
        return false;
    }
    p += strlen(key);
    size_t n = 0;
    while (*p && *p != '"' && n < out_len - 1) {
        unsigned char c = (unsigned char)*p++;
        if (c < 128) {                 /* keep ASCII only: the 5x7 font has no Cyrillic */
            out[n++] = (char)toupper(c);
        }
    }
    out[n] = '\0';
    return n > 0;
}

esp_err_t weather_fetch_city(double lat, double lon, char *out, size_t out_len)
{
    char *buf = malloc(HTTP_BUF_SIZE);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    char url[200];
    snprintf(url, sizeof(url),
             "https://api.bigdatacloud.net/data/reverse-geocode-client"
             "?latitude=%.4f&longitude=%.4f&localityLanguage=en",
             lat, lon);

    esp_err_t err = http_get(url, buf, HTTP_BUF_SIZE);
    if (err == ESP_OK) {
        if (!extract_string(buf, "\"city\":\"", out, out_len) &&
            !extract_string(buf, "\"locality\":\"", out, out_len)) {
            err = ESP_ERR_NOT_FOUND;
        }
    }
    free(buf);
    return err;
}

const char *weather_wmo_text(int c)
{
    if (c == 0)               return "CLEAR";
    if (c >= 1 && c <= 3)     return "CLOUDY";
    if (c == 45 || c == 48)   return "FOG";
    if (c >= 51 && c <= 57)   return "DRIZZLE";
    if (c >= 61 && c <= 67)   return "RAIN";
    if (c >= 71 && c <= 77)   return "SNOW";
    if (c >= 80 && c <= 82)   return "SHOWERS";
    if (c == 85 || c == 86)   return "SNOW SHOWERS";
    if (c >= 95 && c <= 99)   return "STORM";
    return "?";
}
