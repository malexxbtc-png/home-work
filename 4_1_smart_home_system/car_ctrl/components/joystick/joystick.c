#include <stdlib.h>
#include "joystick.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG          "joystick"
#define RAW_MAX      4095
#define CAL_SAMPLES  16
/* Розумний діапазон центру: ~1.65 В на 3.3 В = ~2048. Поза ним джойстик не під'єднано або без живлення. */
#define CENTER_MIN   1000
#define CENTER_MAX   3100

static joystick_config_t     s_cfg;
static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t         s_chx, s_chy;
static int                   s_cx, s_cy;
static bool                  s_btn_prev;

/* На ESP32-S3 ADC1 канали: GPIO1 = CH0, ..., GPIO10 = CH9 */
static esp_err_t gpio_to_adc1(int gpio, adc_channel_t *ch)
{
    if (gpio < 1 || gpio > 10) return ESP_ERR_INVALID_ARG;
    *ch = (adc_channel_t)(gpio - 1);
    return ESP_OK;
}

static int raw_read(adc_channel_t ch)
{
    int v = 0;
    adc_oneshot_read(s_adc, ch, &v);
    return v;
}

static int average_raw(adc_channel_t ch)
{
    long sum = 0;
    for (int i = 0; i < CAL_SAMPLES; i++) sum += raw_read(ch);
    return (int)(sum / CAL_SAMPLES);
}

static int to_percent(int v, int center, bool invert)
{
    int d = v - center;
    if (abs(d) < s_cfg.deadzone) return 0;

    int span = (d > 0) ? (RAW_MAX - center) : center;
    if (span <= 0) return 0;

    int out = d * 100 / span;
    if (out >  100) out =  100;
    if (out < -100) out = -100;
    return invert ? -out : out;
}

esp_err_t joystick_init(const joystick_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;

    ESP_RETURN_ON_ERROR(gpio_to_adc1(cfg->x_gpio, &s_chx), TAG, "вісь X має бути GPIO1..10");
    ESP_RETURN_ON_ERROR(gpio_to_adc1(cfg->y_gpio, &s_chy), TAG, "вісь Y має бути GPIO1..10");

    const adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_adc), TAG, "adc unit");

    const adc_oneshot_chan_cfg_t chc = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_chx, &chc), TAG, "chan X");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_chy, &chc), TAG, "chan Y");

    s_cx = average_raw(s_chx);
    s_cy = average_raw(s_chy);
    ESP_LOGI(TAG, "центр: X=%d Y=%d", s_cx, s_cy);
    if (s_cx < CENTER_MIN || s_cx > CENTER_MAX || s_cy < CENTER_MIN || s_cy > CENTER_MAX) {
        ESP_LOGE(TAG, "центр поза діапазоном: перевірте живлення 3V3 і проводи VRx/VRy");
        return ESP_ERR_INVALID_STATE;
    }

    if (cfg->sw_gpio >= 0) {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->sw_gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "кнопка");
    }
    return ESP_OK;
}

esp_err_t joystick_read(joystick_state_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    out->x = to_percent(raw_read(s_chx), s_cx, false);
    out->y = to_percent(raw_read(s_chy), s_cy, s_cfg.invert_y);

    bool pressed = (s_cfg.sw_gpio >= 0) && (gpio_get_level(s_cfg.sw_gpio) == 0);
    out->pressed_edge = pressed && !s_btn_prev;
    s_btn_prev = pressed;
    return ESP_OK;
}
