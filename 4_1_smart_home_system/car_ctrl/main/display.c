#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.h"
#include "sdkconfig.h"
#include "motor_drive.h"
#include "max7219.h"
#include "telemetry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Розкладка 8x8 (біт 7 рядка = ліва колонка).
 * Текст (шрифт 3x4) у рядках ROW0..ROW0+3:
 *   тяга не 0  — абсолютна швидкість цифрами (0..100), напрямок не показуємо
 *   тяга 0     — бігучий рядок телеметрії */
#define ROW0 2

/* Шрифт 3x4: біти 2..0 = ліва..права колонка, рядки зверху вниз */
static const uint8_t G_DIG[10][4] = {
    {7,5,5,7}, {2,6,2,7}, {7,1,6,7}, {7,1,3,7}, {5,5,7,1},
    {7,4,3,7}, {7,4,7,7}, {7,1,1,1}, {7,5,7,7}, {7,5,7,1},
};
static const uint8_t G_MINUS[4] = {0,0,7,0};
static const uint8_t G_DOT[4]   = {0,0,0,2};
static const uint8_t G_COLON[4] = {0,2,0,2};
static const uint8_t G_PLUS[4]  = {0,2,7,2};
static const uint8_t G_T[4]     = {7,2,2,2};
static const uint8_t G_H[4]     = {5,5,7,5};
static const uint8_t G_P[4]     = {7,5,7,4};
static const uint8_t G_BLANK[4] = {0,0,0,0};

static const uint8_t *glyph(char c)
{
    if (c >= '0' && c <= '9') return G_DIG[c - '0'];
    switch (c) {
        case '-': return G_MINUS;
        case '.': return G_DOT;
        case ':': return G_COLON;
        case '+': return G_PLUS;
        case 'T': return G_T;
        case 'H': return G_H;
        case 'P': return G_P;
        default:  return G_BLANK;
    }
}

#define COLS_MAX 160
static uint8_t s_cols[COLS_MAX];   /* колонки поточного вмісту */
static int     s_len;              /* їх кількість */
static bool    s_scroll;           /* true — прокрутка, false — статично */
static char    s_key[TELE_TEXT_MAX];

/* Текст -> колонки. Пробіл 1 колонка між символами. */
static int text_to_cols(const char *s, uint8_t *out, int max)
{
    int n = 0;
    for (const char *p = s; *p; p++) {
        if (n > 0) {
            if (n >= max) return n;
            out[n++] = 0;
        }
        const uint8_t *g = glyph(*p);
        for (int c = 0; c < 3; c++) {
            if (n >= max) return n;
            uint8_t m = 0;
            for (int r = 0; r < 4; r++) {
                if ((g[r] >> (2 - c)) & 1) m |= (uint8_t)(1 << r);
            }
            out[n++] = m;
        }
    }
    return n;
}

/* Встановлює вміст: прокрутка (з порожніми колонками на вході й виході) або статично */
static void set_content(const char *key, bool scroll)
{
    snprintf(s_key, sizeof(s_key), "%s", key);
    s_scroll = scroll;
    if (scroll) {
        for (int i = 0; i < 8; i++) s_cols[i] = 0;
        int n = 8 + text_to_cols(key, &s_cols[8], COLS_MAX - 16);
        for (int i = 0; i < 8; i++) s_cols[n++] = 0;
        s_len = n;
    } else {
        s_len = text_to_cols(key, s_cols, COLS_MAX);
    }
}

/* Малює 4 рядки вікном колонок з позиції pos */
static void draw_window(uint8_t rows[8], int pos)
{
    for (int x = 0; x < 8; x++) {
        const uint8_t m = s_cols[pos + x];
        for (int r = 0; r < 4; r++) {
            if (m & (1 << r)) rows[ROW0 + r] |= (uint8_t)(0x80 >> x);
        }
    }
}

/* Центрує короткий статичний текст (до 8 колонок) */
static void draw_static(uint8_t rows[8])
{
    const int off = (8 - s_len) / 2;
    for (int x = 0; x < 8; x++) {
        const int i = x - off;
        const uint8_t m = (i >= 0 && i < s_len) ? s_cols[i] : 0;
        for (int r = 0; r < 4; r++) {
            if (m & (1 << r)) rows[ROW0 + r] |= (uint8_t)(0x80 >> x);
        }
    }
}

static void display_task(void *arg)
{
    char    key[TELE_TEXT_MAX];
    uint8_t rows[8];
    int     pos = 0;

    for (;;) {
        const int speed = motor_drive_get_percent();

        bool scroll;
        if (speed == 0) {
            telemetry_get_text(key, sizeof(key));
            scroll = true;
        } else {
            snprintf(key, sizeof(key), "%d", abs(speed));
            scroll = strlen(key) > 2;          /* 0..99 статично, 100 — прокрутка */
        }

        /* Позицію прокрутки не скидаємо: телеметрія міняється щосекунди */
        if (strcmp(key, s_key) != 0) {
            set_content(key, scroll);
        }

        memset(rows, 0, 8);
        if (s_scroll) {
            if (pos >= s_len - 7) pos = 0;
            draw_window(rows, pos);
            pos = (pos + 1) % (s_len - 7);
        } else {
            draw_static(rows);
        }
        max7219_draw(rows);

        vTaskDelay(pdMS_TO_TICKS(CONFIG_CAR_SCROLL_MS));
    }
}

esp_err_t display_start(void)
{
    set_content("", true);
    if (xTaskCreate(display_task, "display", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
