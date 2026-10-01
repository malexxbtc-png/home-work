#ifndef BUZZER_H
#define BUZZER_H

#include "melodies.h"

#define BUZZER_GPIO 25

void buzzer_init(void);
void buzzer_play_tone(int frequency, int duration_ms);
void buzzer_play_melody_async(int melody_id);

#endif // BUZZER_H