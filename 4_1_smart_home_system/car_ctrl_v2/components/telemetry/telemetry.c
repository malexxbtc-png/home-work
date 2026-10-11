#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "telemetry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG            "telemetry"
#define FRAME_BODY_MAX 64
#define STALE_US       (3LL * 1000 * 1000)   /* старше 3 с — вважаємо, що даних немає */

static SemaphoreHandle_t s_lock;
static char              s_text[TELE_TEXT_MAX];
static bool              s_have;
static int64_t           s_last_us;

esp_err_t telemetry_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Повертає початок поля до ',' (рядок змінюється на місці). *p стає на наступне поле або NULL. */
static char *next_field(char **p)
{
    char *start = *p;
    if (!start) return NULL;
    char *comma = strchr(start, ',');
    if (comma) {
        *comma = '\0';
        *p = comma + 1;
    } else {
        *p = NULL;
    }
    return start;
}

bool telemetry_parse_sens(const char *line, telemetry_frame_t *out)
{
    if (!line || !out || line[0] != '$' || strncmp(line + 1, "SENS,", 5) != 0) {
        return false;
    }
    const char *star = strchr(line, '*');
    if (!star) return false;

    uint8_t cs = 0;
    for (const char *p = line + 1; p < star; p++) {
        cs ^= (uint8_t)*p;
    }
    if (cs != (uint8_t)strtol(star + 1, NULL, 16)) {
        return false;
    }

    size_t blen = (size_t)(star - (line + 1));
    if (blen == 0 || blen >= FRAME_BODY_MAX) return false;

    char body[FRAME_BODY_MAX];
    memcpy(body, line + 1, blen);
    body[blen] = '\0';

    char *p  = body;
    next_field(&p);                       /* "SENS" */
    char *ts = next_field(&p);
    char *hs = next_field(&p);
    char *ps = next_field(&p);
    if (!ts || !hs || !ps) return false;

    char *end = NULL;
    float t = strtof(ts, &end);
    if (end == ts) return false;

    float pr = strtof(ps, &end);
    if (end == ps) return false;

    out->temp_c    = t;
    out->press_hpa = pr;

    if (strcmp(hs, "NA") == 0) {
        out->has_hum = false;
        out->hum_pct = 0.0f;
    } else {
        float h = strtof(hs, &end);
        if (end == hs) return false;
        out->has_hum = true;
        out->hum_pct = h;
    }
    return true;
}

bool telemetry_update(const char *line)
{
    telemetry_frame_t f;
    if (!telemetry_parse_sens(line, &f)) {
        ESP_LOGW(TAG, "рядок не прийнято: %s", line);
        return false;
    }

    char text[TELE_TEXT_MAX];
    if (f.has_hum) {
        snprintf(text, sizeof(text), "T%.1f H%.0f P%.0f", f.temp_c, f.hum_pct, f.press_hpa);
    } else {
        snprintf(text, sizeof(text), "T%.1f H-- P%.0f", f.temp_c, f.press_hpa);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_text, sizeof(s_text), "%s", text);
    s_have    = true;
    s_last_us = esp_timer_get_time();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "телеметрія: %s", text);
    return true;
}

void telemetry_get_text(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool fresh = s_have && (esp_timer_get_time() - s_last_us) <= STALE_US;
    if (fresh) {
        snprintf(buf, len, "%s", s_text);
    } else {
        /* немає свіжих даних — показуємо прочерки, щоб було видно */
        snprintf(buf, len, "T-- H-- P--");
    }
    xSemaphoreGive(s_lock);
}
