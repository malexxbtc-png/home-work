#ifndef WX_JSON_H
#define WX_JSON_H

#include <stdbool.h>
#include <stddef.h>

#define NETWX_MAX_DAYS 7

/* Погода, отримана від Open-Meteo (без залежностей від ESP-IDF, щоб перевіряти на ПК) */
typedef struct {
    float temp;                  /* °C */
    float rh;                    /* %, NAN якщо немає */
    float rain, snow;            /* мм, см */
    float wind, gust, dir;       /* м/с, м/с, градуси */
    int code;                    /* WMO */
    int cloud;                   /* % */
    int n_days;
    struct {
        int cond;
        float tmin, tmax, pop, wmax;
    } day[NETWX_MAX_DAYS];
} net_wx_t;

/* Розбирає відповідь Open-Meteo (current + daily). false, якщо немає обов'язкових полів */
bool wx_json_parse_open_meteo(const char *body, net_wx_t *out);

/* Будує повідомлення для UART і для розбору на майстрі:
   {"t":"wx","age":S,"cur":{"T","w","g","dir","rain","snow","RH","cloud","code"},
    "fc":[{"cond","tmin","tmax","pop","wmax"},...]}
   age = секунд від моменту запиту до Open-Meteo. Повертає довжину або -1. */
int wx_json_build(const net_wx_t *w, int age_s, char *dest, size_t size);

#endif
