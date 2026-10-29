#ifndef KEYS_LOGIC_H
#define KEYS_LOGIC_H

/* Чиста логіка резистивної клавіатури (без заліза): класифікація напруги, дебаунс, автоповтор,
   пошук стабільного рівня для калібрування. Тестується на ПК. */

#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_NONE = 0, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_SELECT, KEY_COUNT } key_id_t;
typedef enum { KEY_EV_NONE = 0, KEY_EV_PRESS, KEY_EV_REPEAT, KEY_EV_RELEASE } key_ev_type_t;

typedef struct {
    key_id_t key;
    key_ev_type_t type;
} key_event_t;

typedef struct {
    int nominal_mv[KEY_COUNT];   /* індекс = key_id_t (1..5); [0] не використовується */
    int idle_mv;                 /* напруга без натискання */
} key_cal_t;

void keys_default_cal(key_cal_t *cal);
bool keys_cal_valid(const key_cal_t *cal);       /* усі значення різні й менші за idle */
const char *keys_name(key_id_t key);

/* Яка кнопка відповідає напрузі; допуск кожної = мін(KEY_TOLERANCE_MAX, половина відстані до сусіда) */
key_id_t keys_classify(const key_cal_t *cal, int mv);

typedef struct {
    key_cal_t cal;
    key_id_t candidate;
    uint32_t candidate_since;
    key_id_t active;
    uint32_t next_repeat;
    bool release_pending;        /* треба видати RELEASE перед наступним PRESS */
} key_state_t;

void keys_state_init(key_state_t *st, const key_cal_t *cal);

/* Викликати кожні ~10 мс з усередненою напругою. Повертає не більше однієї події за виклик. */
key_event_t keys_feed(key_state_t *st, int mv, uint32_t now_ms);

/* Детектор стабільного рівня: напруга в межах spread_mv протягом hold_ms. */
typedef struct {
    int min_mv, max_mv;
    int64_t sum;
    int n;
    uint32_t since;
    bool running;
} key_stable_t;

void keys_stable_reset(key_stable_t *s);
/* true, коли рівень стабільний; у *avg_mv середнє */
bool keys_stable_feed(key_stable_t *s, int mv, uint32_t now_ms, int spread_mv, uint32_t hold_ms, int *avg_mv);

#endif
