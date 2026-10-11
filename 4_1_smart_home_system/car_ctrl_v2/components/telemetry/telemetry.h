#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define TELE_TEXT_MAX 32

/* Формат від погодної станції:
 *   $SENS,<temp C>,<hum %|NA>,<press hPa>*<XOR hex>
 * XOR — по всіх байтах між '$' і '*'. */
typedef struct {
    float temp_c;
    bool  has_hum;
    float hum_pct;
    float press_hpa;
} telemetry_frame_t;

esp_err_t telemetry_init(void);

/* Чистий парсер (без стану) — зручно тестувати. */
bool telemetry_parse_sens(const char *line, telemetry_frame_t *out);

/* Парсить рядок і зберігає текст для показу, наприклад "T23.5 H45 P1013".
 * Повертає false, якщо рядок не прийнято. */
bool telemetry_update(const char *line);

/* Копія останнього тексту телеметрії (потокобезпечно). */
void telemetry_get_text(char *buf, size_t len);
