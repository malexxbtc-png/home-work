#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "keys_logic.h"
#include "ssd1306.h"
#include "status.h"
#include "wx_data.h"

typedef enum {
    SCR_HOME = 0,
    SCR_METEO,      /* локальні датчики (STM32) */
    SCR_WX,         /* Open-Meteo: зараз / прогноз */
    SCR_TIME,       /* дата і час */
    SCR_EVENT,      /* остання подія і історія */
    SCR_STATUS,     /* стан модулів */
    SCR_KEYS,       /* тест клавіатури */
    SCR_COUNT
} ui_screen_t;

typedef enum { UI_ACT_NONE = 0, UI_ACT_PHOTO } ui_action_t;

typedef struct {
    int screen;
    int sub;                    /* підсторінка (WX: 0 зараз, 1 прогноз) */
    int ev_idx;                 /* яка подія показана: 0 = остання */
    uint32_t last_input_ms;
    char toast[24];
    uint32_t toast_until_ms;
} ui_t;

/* Знімок даних для малювання */
typedef struct {
    bool time_ok;
    struct tm tm;
    system_status_t st;
    wx_view_t wx;
    int key_mv;
    key_id_t key_now;
    key_cal_t cal;
    uint32_t now_ms;
} ui_view_t;

void ui_init(ui_t *ui, uint32_t now_ms);
ui_action_t ui_on_key(ui_t *ui, key_event_t ev, uint32_t now_ms);
void ui_tick(ui_t *ui, uint32_t now_ms);                       /* повернення на HOME за бездіяльності */
void ui_toast(ui_t *ui, const char *text, uint32_t now_ms, uint32_t duration_ms);
void ui_render(const ui_t *ui, ssd1306_handle_t oled, const ui_view_t *view);

const char *ui_screen_title(int screen);

#endif
