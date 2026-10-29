#include "keys_cal.h"

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keys_adc.h"

static const char *TAG = "keys_cal";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void show(ssd1306_handle_t oled, const char *l1, const char *l2, const char *l3)
{
    ssd1306_clear(oled, false);
    ssd1306_draw_string(oled, 0, 0, "KEY CALIBRATION");
    ssd1306_invert_rect(oled, 0, 0, 128, 9);
    if (l1) ssd1306_draw_string_scaled(oled, 0, 16, l1, 2);
    if (l2) ssd1306_draw_string(oled, 0, 40, l2);
    if (l3) ssd1306_draw_string(oled, 0, 50, l3);
    ssd1306_show(oled);
}

/* Чекає стабільного рівня. mode: 0 = будь-який, -1 = лише нижче за max_mv, +1 = лише вище за min_mv */
static bool wait_level(int mode, int limit_mv, uint32_t timeout_ms, int *avg)
{
    key_stable_t st;
    keys_stable_reset(&st);
    uint32_t start = now_ms();
    while ((uint32_t)(now_ms() - start) < timeout_ms) {
        int mv = keys_adc_read_mv();
        if (mv >= 0) {
            bool allowed = mode == 0 || (mode < 0 && mv < limit_mv) || (mode > 0 && mv > limit_mv);
            if (!allowed) {
                keys_stable_reset(&st);
            } else if (keys_stable_feed(&st, mv, now_ms(), 40, 500, avg)) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

bool keys_cal_run_if_requested(ssd1306_handle_t oled)
{
    if (oled == NULL) {
        return false;
    }
    int held;
    if (!wait_level(-1, KEY_IDLE_MV - 400, 800, &held)) {
        return false;                            /* на старті нічого не утримується */
    }
    ESP_LOGW(TAG, "Key held at boot (%d mV): calibration", held);

    key_cal_t cal;
    keys_default_cal(&cal);
    int idle;
    show(oled, "RELEASE", "all keys", NULL);
    if (!wait_level(+1, 1000, 15000, &idle)) {
        show(oled, "TIMEOUT", "not saved", NULL);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return false;
    }
    cal.idle_mv = idle;

    static const key_id_t order[] = {KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_SELECT};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        char prompt[16], progress[24], idle_text[24];
        snprintf(prompt, sizeof(prompt), "%s", keys_name(order[i]));
        snprintf(progress, sizeof(progress), "Press and hold %d/5", (int)i + 1);
        snprintf(idle_text, sizeof(idle_text), "idle %d mV", idle);
        show(oled, prompt, progress, idle_text);
        int mv;
        if (!wait_level(-1, idle - 120, 20000, &mv)) {
            show(oled, "TIMEOUT", "not saved", NULL);
            vTaskDelay(pdMS_TO_TICKS(1500));
            return false;
        }
        cal.nominal_mv[order[i]] = mv;
        ESP_LOGI(TAG, "%s = %d mV", keys_name(order[i]), mv);
        show(oled, "RELEASE", prompt, NULL);
        if (!wait_level(+1, idle - 100, 10000, &mv)) {
            return false;
        }
    }

    if (keys_adc_set_cal(&cal, true) != ESP_OK) {
        show(oled, "INVALID", "values too close", "not saved");
        vTaskDelay(pdMS_TO_TICKS(2500));
        return false;
    }
    show(oled, "SAVED", "keys calibrated", NULL);
    vTaskDelay(pdMS_TO_TICKS(1500));
    return true;
}
