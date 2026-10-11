#include <stdbool.h>
#include <stdint.h>
#include "steering.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "steering"

/* Half-step, порядок IN1..IN4 */
static const uint8_t SEQ[8][4] = {
    {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
    {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1},
};

static steering_config_t s_cfg;
static volatile int32_t  s_pos;     /* поточна позиція, half-steps */
static volatile int32_t  s_target;  /* ціль, half-steps */
static volatile int      s_angle;   /* ціль, градуси */

/* phase < 0 — вимкнути всі котушки */
static void coils(int phase)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(s_cfg.gpio[i], phase >= 0 ? SEQ[phase][i] : 0);
    }
}

static void steer_task(void *arg)
{
    bool energized = false;

    for (;;) {
        int32_t pos = s_pos;
        int32_t tgt = s_target;

        if (pos == tgt) {
            if (energized) {               /* не тримаємо котушки під напругою */
                coils(-1);
                energized = false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int32_t next = pos + (tgt > pos ? 1 : -1);
        coils(((next % 8) + 8) % 8);       /* фаза = позиція mod 8 */
        energized = true;
        s_pos = next;
        vTaskDelay(pdMS_TO_TICKS(s_cfg.step_delay_ms));
    }
}

esp_err_t steering_init(const steering_config_t *cfg)
{
    if (!cfg || cfg->halfsteps_per_rev <= 0 || cfg->step_delay_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    uint64_t mask = 0;
    for (int i = 0; i < 4; i++) mask |= 1ULL << cfg->gpio[i];
    const gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    coils(-1);

    if (xTaskCreatePinnedToCore(steer_task, "steer", 3072, NULL, 5, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t steering_set_angle(int deg)
{
    if (deg >  s_cfg.max_angle_deg) deg =  s_cfg.max_angle_deg;
    if (deg < -s_cfg.max_angle_deg) deg = -s_cfg.max_angle_deg;

    s_angle  = deg;
    s_target = (int32_t)deg * s_cfg.halfsteps_per_rev / 360;
    return ESP_OK;
}

int steering_get_angle(void)
{
    return s_angle;
}

bool steering_is_moving(void)
{
    return s_pos != s_target;
}
