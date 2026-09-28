#include "buzzer_player.h"
#include "sound_dac.h"
#include "sound_pwm.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "BUZZER_PLAYER";

static sound_mode_t active_mode = SOUND_MODE_DAC;
static const note_t *current_melody = NULL;
static size_t melody_len = 0;
static size_t current_idx = 0;
static uint8_t ticks_left = 0;
static bool is_playing = false;
static bool note_is_sounding = false;

static buzzer_tick_cb_t user_tick_callback = NULL;

static void set_hardware_tone(uint16_t freq) {
    note_is_sounding = (freq != NOTE_REST);
    if (active_mode == SOUND_MODE_DAC) {
        sound_dac_set_tone(freq);
    } else {
        sound_pwm_set_tone(freq);
    }
}

static void timer_tick_cb(void *arg) {
    if (!is_playing || current_melody == NULL) return;

    if (ticks_left == 0) {
        if (current_idx >= melody_len) {
            current_idx = 0; // Повтор
        }

        note_t note = current_melody[current_idx];
        set_hardware_tone(note.freq);
        ticks_left = note.ticks;
        current_idx++;
    }

    if (user_tick_callback) {
        user_tick_callback(note_is_sounding);
    }

    ticks_left--;
}

void buzzer_player_init(sound_mode_t mode, gpio_num_t sound_gpio, buzzer_tick_cb_t on_tick_cb) {
    active_mode = mode;
    user_tick_callback = on_tick_cb;

    if (active_mode == SOUND_MODE_DAC) {
        sound_dac_init(sound_gpio);
    } else {
        sound_pwm_init(sound_gpio);
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &timer_tick_cb,
        .name = "buzzer_tick"
    };
    esp_timer_handle_t timer_handle;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle, TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Плеєр ініціалізовано у режимі %s", (mode == SOUND_MODE_DAC) ? "DAC" : "PWM");
}

void buzzer_player_start(const note_t *melody, size_t length) {
    current_melody = melody;
    melody_len = length;
    current_idx = 0;
    ticks_left = 0;
    is_playing = true;
}

void buzzer_player_stop(void) {
    is_playing = false;
    set_hardware_tone(NOTE_REST);
}