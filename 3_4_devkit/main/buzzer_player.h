#pragma once
#include "driver/gpio.h"

#define TICK_PERIOD_MS 50

// Ноти 6-ї та 7-ї октав (резонансний діапазон п'єзовипромінювача)
#define NOTE_REST 0
#define NOTE_C6   1047
#define NOTE_D6   1175
#define NOTE_E6   1319
#define NOTE_F6   1397
#define NOTE_G6   1568
#define NOTE_A6   1760
#define NOTE_B6   1975
#define NOTE_C7   2093
#define NOTE_E7   2637
#define NOTE_G7   3136

typedef enum {
    SOUND_MODE_DAC,
    SOUND_MODE_PWM
} sound_mode_t;

typedef struct {
    uint16_t freq;
    uint8_t ticks;
} note_t;

typedef void (*buzzer_tick_cb_t)(bool is_note_playing);

void buzzer_player_init(sound_mode_t mode, gpio_num_t sound_gpio, buzzer_tick_cb_t on_tick_cb);
void buzzer_player_start(const note_t *melody, size_t length);
void buzzer_player_stop(void);