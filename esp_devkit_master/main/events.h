#ifndef EVENTS_H
#define EVENTS_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

typedef enum { PH_NONE = 0, PH_PENDING, PH_OK, PH_FAIL, PH_SKIP } photo_state_t;

typedef struct {
    uint32_t seq;           /* порядковий номер події (з 1) */
    char stamp[20];         /* "25.10 19:45:12" або "up 01:23:45" */
    char type[12];          /* pir, wind, fog, ice, rain, snow, stm_lost, stm_ok, esp_lost, esp_ok, manual, boot */
    char info[20];
    char file[48];          /* відносний шлях знімка на SD (або текст помилки) */
    photo_state_t photo;
} event_t;

/* Завантажує останню подію з NVS (якщо була) */
void events_init(void);

/* Додає подію, повертає її seq */
uint32_t events_add(const char *stamp, const char *type, const char *info, photo_state_t photo);

/* Оновлює стан знімка події (якщо вона ще в журналі) */
void events_set_photo(uint32_t seq, photo_state_t state, const char *file);

int events_count(void);

/* back = 0 — остання подія, 1 — попередня ... */
bool events_get(int back, event_t *out);

#endif
