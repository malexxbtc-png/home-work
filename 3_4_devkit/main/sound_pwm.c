#include "sound_pwm.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SOUND_PWM";

#define PWM_MODE      LEDC_LOW_SPEED_MODE
#define PWM_TIMER     LEDC_TIMER_0
#define PWM_CHANNEL   LEDC_CHANNEL_0
#define PWM_RES       LEDC_TIMER_8_BIT

void sound_pwm_init(gpio_num_t gpio_num) {
    ledc_timer_config_t timer_conf = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .speed_mode = PWM_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = gpio_num,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    
}

void sound_pwm_set_tone(uint16_t freq) {
    if (freq == 0) {
        ledc_set_duty(PWM_MODE, PWM_CHANNEL, 0);
        ledc_update_duty(PWM_MODE, PWM_CHANNEL);
    } else {
        ledc_set_freq(PWM_MODE, PWM_TIMER, freq);
        ledc_set_duty(PWM_MODE, PWM_CHANNEL, 128); // 50% скважність
        ledc_update_duty(PWM_MODE, PWM_CHANNEL);
    }
}

void sound_pwm_stop(void) {
    sound_pwm_set_tone(0);
}