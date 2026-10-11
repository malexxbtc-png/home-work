#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "melodies.h"

#define BUZZER_GPIO 25

void buzzer_init(void);
void buzzer_play_tone(int frequency, int duration_ms);
void buzzer_play_melody_async(int melody_id);
void buzzer_play_melody_loop_async(int melody_id, uint32_t duration_sec);

#endif // BUZZER_H