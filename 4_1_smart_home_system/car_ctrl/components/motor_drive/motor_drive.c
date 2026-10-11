#include <stdlib.h>
#include "motor_drive.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG          "motor"
#define LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_N LEDC_TIMER_0
#define LEDC_CH      LEDC_CHANNEL_0
#define DUTY_BITS    LEDC_TIMER_10_BIT
#define DUTY_MAX     1023

static motor_drive_config_t s_cfg;
static SemaphoreHandle_t    s_lock;
static int                  s_percent;

/* Ненульова тяга: 1..100 -> min_duty..max_duty %, далі в сирі одиниці LEDC.
 * Тяга 0 — повна зупинка (шпаруватість 0). */
static uint32_t duty_for_percent(int percent)
{
    if (percent == 0) {
        return 0;
    }
    const int mag = abs(percent);
    const int eff = s_cfg.min_duty_pct + mag * (s_cfg.max_duty_pct - s_cfg.min_duty_pct) / 100;
    return (uint32_t)eff * DUTY_MAX / 100;
}

esp_err_t motor_drive_init(const motor_drive_config_t *cfg)
{
    if (!cfg || cfg->min_duty_pct < 0 || cfg->max_duty_pct > 100 ||
        cfg->min_duty_pct >= cfg->max_duty_pct) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    uint64_t mask = 0;
    if (cfg->in1_gpio >= 0) mask |= 1ULL << cfg->in1_gpio;
    if (cfg->in2_gpio >= 0) mask |= 1ULL << cfg->in2_gpio;
    if (mask) {
        const gpio_config_t io = {
            .pin_bit_mask = mask,
            .mode         = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio");
    }

    const ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER_N,
        .duty_resolution = DUTY_BITS,
        .freq_hz         = cfg->pwm_freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "timer");

    const ledc_channel_config_t ch = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH,
        .timer_sel  = LEDC_TIMER_N,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = cfg->ena_gpio,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "channel");

    return motor_drive_set_percent(0);
}

esp_err_t motor_drive_set_percent(int percent)
{
    if (percent > 100)  percent = 100;
    if (percent < -100) percent = -100;

    if (s_cfg.in1_gpio < 0) {
        /* Один MOSFET: реверсу немає, від'ємна тяга = стоп */
        if (percent < 0) percent = 0;
    } else {
        /* Напрямок: IN1/IN2. Обидва 0 — стоп. */
        gpio_set_level(s_cfg.in1_gpio, percent > 0);
        gpio_set_level(s_cfg.in2_gpio, percent < 0);
    }

    const uint32_t duty = duty_for_percent(percent);
    if (percent != s_percent) {                 /* лог лише при зміні, не на кожному циклі */
        ESP_LOGI(TAG, "тяга %d %%, шпаруватість %lu/1023", percent, (unsigned long)duty);
    }
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_MODE, LEDC_CH, duty), TAG, "duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_MODE, LEDC_CH), TAG, "update");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_percent = percent;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

int motor_drive_get_percent(void)
{
    int v;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    v = s_percent;
    xSemaphoreGive(s_lock);
    return v;
}
