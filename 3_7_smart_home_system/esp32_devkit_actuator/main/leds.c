#include "leds.h"
#include "driver/ledc.h"

#define LED_COUNT 6
static const int led_gpios[LED_COUNT] = {17, 18, 19, 21, 22, 23};
static uint8_t led_levels[LED_COUNT] = {0};
static const uint32_t duty_map[5] = {0, 2048, 4096, 6144, 8191};

void leds_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_1,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    for (int i = 0; i < LED_COUNT; i++) {
        ledc_channel_config_t channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = (ledc_channel_t)i,
            .timer_sel      = LEDC_TIMER_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = led_gpios[i],
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&channel);
    }
}

void led_set_level(uint8_t led_index, uint8_t level) {
    if (led_index >= LED_COUNT || level > 4) return;
    led_levels[led_index] = level;
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)led_index, duty_map[level]);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)led_index);
}

uint8_t led_get_level(uint8_t led_index) {
    if (led_index >= LED_COUNT) return 0;
    return led_levels[led_index];
}