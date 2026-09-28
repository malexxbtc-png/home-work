#include "led_effects.h"

static bool toggle_state = false;

void led_effects_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED1_GPIO) | (1ULL << LED2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    gpio_set_level(LED1_GPIO, 0);
    gpio_set_level(LED2_GPIO, 0);
}

void led_effects_update(bool is_note_playing) {
    if (is_note_playing) {
        toggle_state = !toggle_state;
        gpio_set_level(LED1_GPIO, toggle_state ? 1 : 0);
        gpio_set_level(LED2_GPIO, toggle_state ? 0 : 1);
    } else {
        gpio_set_level(LED1_GPIO, 0);
        gpio_set_level(LED2_GPIO, 0);
    }
}