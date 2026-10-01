#include "servo.h"
#include "driver/ledc.h"

static uint8_t current_angle = 0;

void servo_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_2,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .freq_hz          = 50,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_6,
        .timer_sel      = LEDC_TIMER_2,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&channel);
    
    servo_set_angle(0);
}

void servo_set_angle(uint8_t angle) {
    if (angle > 180) angle = 180;
    current_angle = angle;

    uint32_t duty = 205 + ((angle * (1024 - 205)) / 180);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6);
}

uint8_t servo_get_angle(void) {
    return current_angle;
}