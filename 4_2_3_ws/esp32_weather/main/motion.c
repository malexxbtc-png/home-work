#include "motion.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"

#define PIR_GPIO ((gpio_num_t)CONFIG_WS_PIR_GPIO)

/* Milliseconds (32-bit, wraps after ~49 days; unsigned subtraction handles it).
 * 32-bit so the ISR write and the task read are atomic. */
static volatile uint32_t s_last_motion_ms;

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void IRAM_ATTR pir_isr(void *arg)
{
    s_last_motion_ms = now_ms();
}

esp_err_t motion_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIR_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* defined level if the sensor is unplugged */
        .intr_type = GPIO_INTR_POSEDGE,
    };
    /* The AM312 needs ~10 s to settle after power-on (HC-SR501: 30-60 s); treat boot as "motion".
     * The AM312 output stays high only ~2 s per trigger, which is fine for the idle timer. */
    s_last_motion_ms = now_ms();

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  /* already installed is fine */
        return err;
    }
    return gpio_isr_handler_add(PIR_GPIO, pir_isr, NULL);
}

uint32_t motion_idle_seconds(void)
{
    if (gpio_get_level(PIR_GPIO)) {      /* output still high = ongoing motion */
        s_last_motion_ms = now_ms();
        return 0;
    }
    return (now_ms() - s_last_motion_ms) / 1000;
}
