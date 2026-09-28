#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "buzzer_player.h"
#include "led_effects.h"

static const note_t jingle_bells_high[] = {
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2},
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2},
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_G6, 4}, {NOTE_REST, 1}, {NOTE_C6, 4}, {NOTE_REST, 1}, {NOTE_D6, 4}, {NOTE_REST, 1},
    {NOTE_E6, 12}, {NOTE_REST, 4},{NOTE_REST, 4}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2},
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2},
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_G6, 4}, {NOTE_REST, 1}, {NOTE_C6, 4}, {NOTE_REST, 1},
    {NOTE_D6, 4}, {NOTE_REST, 1}, {NOTE_E6, 12}, {NOTE_REST, 4}, {NOTE_REST, 4}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2},
    {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 4}, {NOTE_REST, 1}, {NOTE_E6, 8}, {NOTE_REST, 2}
};

void app_main(void) {
    // 1. Ініціалізація світлодіодів
    led_effects_init();

    // 2. Ініціалізація плеєра:

    //    Варіант A (DAC): SOUND_MODE_DAC, GPIO_NUM_25
    //    Варіант B (PWM): SOUND_MODE_PWM, GPIO_NUM_25


    //buzzer_player_init(SOUND_MODE_DAC, GPIO_NUM_25, led_effects_update);

  
    
    buzzer_player_init(SOUND_MODE_PWM, GPIO_NUM_25, led_effects_update);


   
    buzzer_player_start(jingle_bells_high, sizeof(jingle_bells_high) / sizeof(jingle_bells_high[0]));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}