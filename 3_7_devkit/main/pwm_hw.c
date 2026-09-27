#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // 13 біт (8192 відліки)
#define LEDC_FREQUENCY          50                // 50 Гц для сервоприводів (кадр 20 мс)

#define PERIOD_US               20000u
#define PWM_PERIOD_TICKS        (1u << 13)

void pwm_hw_init(int gpio)
{
    // 1. Налаштування таймера LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Налаштування каналу LEDC
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = gpio,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

void pwm_hw_set_pulse_us(uint32_t pulse_us)
{
    if (pulse_us > PERIOD_US) {
        pulse_us = PERIOD_US;
    }
    
    // Розрахунок скважності для 13-бітної роздільної здатності (8192 відліки на 20 мс)
    uint32_t duty = (pulse_us * PWM_PERIOD_TICKS) / PERIOD_US;
    
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}