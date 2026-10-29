#include "keys_logic.h"
#include <stdlib.h>
#include "config.h"

void keys_default_cal(key_cal_t *cal)
{
    cal->nominal_mv[KEY_NONE] = 0;
    cal->nominal_mv[KEY_RIGHT] = KEY_NOM_RIGHT_MV;
    cal->nominal_mv[KEY_UP] = KEY_NOM_UP_MV;
    cal->nominal_mv[KEY_DOWN] = KEY_NOM_DOWN_MV;
    cal->nominal_mv[KEY_LEFT] = KEY_NOM_LEFT_MV;
    cal->nominal_mv[KEY_SELECT] = KEY_NOM_SELECT_MV;
    cal->idle_mv = KEY_IDLE_MV;
}

const char *keys_name(key_id_t key)
{
    switch (key) {
    case KEY_RIGHT:  return "RIGHT";
    case KEY_UP:     return "UP";
    case KEY_DOWN:   return "DOWN";
    case KEY_LEFT:   return "LEFT";
    case KEY_SELECT: return "SELECT";
    default:         return "-";
    }
}

bool keys_cal_valid(const key_cal_t *cal)
{
    for (int a = KEY_RIGHT; a < KEY_COUNT; a++) {
        if (cal->nominal_mv[a] < 0 || cal->nominal_mv[a] > 3300 || cal->nominal_mv[a] + 120 > cal->idle_mv) {
            return false;
        }
        for (int b = a + 1; b < KEY_COUNT; b++) {
            if (abs(cal->nominal_mv[a] - cal->nominal_mv[b]) < 80) {
                return false;
            }
        }
    }
    return cal->idle_mv > 500 && cal->idle_mv <= 3400;
}

static int tolerance_for(const key_cal_t *cal, int key)
{
    int nearest = abs(cal->idle_mv - cal->nominal_mv[key]);
    for (int k = KEY_RIGHT; k < KEY_COUNT; k++) {
        if (k != key) {
            int d = abs(cal->nominal_mv[k] - cal->nominal_mv[key]);
            if (d < nearest) nearest = d;
        }
    }
    int tol = nearest / 2 - 10;
    if (tol > KEY_TOLERANCE_MAX) tol = KEY_TOLERANCE_MAX;
    if (tol < 20) tol = 20;
    return tol;
}

key_id_t keys_classify(const key_cal_t *cal, int mv)
{
    key_id_t best = KEY_NONE;
    int best_d = 1 << 30;
    for (int k = KEY_RIGHT; k < KEY_COUNT; k++) {
        int d = abs(mv - cal->nominal_mv[k]);
        if (d <= tolerance_for(cal, k) && d < best_d) {
            best = (key_id_t)k;
            best_d = d;
        }
    }
    return best;
}

void keys_state_init(key_state_t *st, const key_cal_t *cal)
{
    st->cal = *cal;
    st->candidate = KEY_NONE;
    st->candidate_since = 0;
    st->active = KEY_NONE;
    st->next_repeat = 0;
    st->release_pending = false;
}

key_event_t keys_feed(key_state_t *st, int mv, uint32_t now_ms)
{
    key_event_t ev = { KEY_NONE, KEY_EV_NONE };
    key_id_t raw = keys_classify(&st->cal, mv);

    if (raw != st->candidate) {
        st->candidate = raw;
        st->candidate_since = now_ms;
    }
    bool stable = (uint32_t)(now_ms - st->candidate_since) >= KEY_DEBOUNCE_MS;

    if (stable && st->candidate != st->active) {
        if (st->active != KEY_NONE) {          /* спершу відпускання попередньої */
            ev.key = st->active;
            ev.type = KEY_EV_RELEASE;
            st->active = KEY_NONE;
            return ev;
        }
        st->active = st->candidate;            /* тут candidate != NONE */
        st->next_repeat = now_ms + KEY_REPEAT_FIRST_MS;
        ev.key = st->active;
        ev.type = KEY_EV_PRESS;
        return ev;
    }
    if (st->active != KEY_NONE && st->candidate == st->active &&
        (int32_t)(now_ms - st->next_repeat) >= 0) {
        st->next_repeat = now_ms + KEY_REPEAT_MS;
        ev.key = st->active;
        ev.type = KEY_EV_REPEAT;
    }
    return ev;
}

void keys_stable_reset(key_stable_t *s)
{
    s->running = false;
    s->n = 0;
    s->sum = 0;
}

bool keys_stable_feed(key_stable_t *s, int mv, uint32_t now_ms, int spread_mv, uint32_t hold_ms, int *avg_mv)
{
    if (!s->running) {
        s->running = true;
        s->min_mv = s->max_mv = mv;
        s->sum = mv;
        s->n = 1;
        s->since = now_ms;
        return false;
    }
    int lo = mv < s->min_mv ? mv : s->min_mv;
    int hi = mv > s->max_mv ? mv : s->max_mv;
    if (hi - lo > spread_mv) {                  /* рівень поплив: починаємо спочатку */
        s->min_mv = s->max_mv = mv;
        s->sum = mv;
        s->n = 1;
        s->since = now_ms;
        return false;
    }
    s->min_mv = lo;
    s->max_mv = hi;
    s->sum += mv;
    s->n++;
    if ((uint32_t)(now_ms - s->since) >= hold_ms) {
        if (avg_mv) *avg_mv = (int)(s->sum / s->n);
        return true;
    }
    return false;
}
