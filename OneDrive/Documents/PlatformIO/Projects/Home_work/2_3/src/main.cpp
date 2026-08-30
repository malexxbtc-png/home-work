#include <Arduino.h>
#include "config.h"
#include "blinker.h"

Blinker led1(LED1_PIN, LED1_INTERVAL);
Blinker led2(LED2_PIN, LED2_INTERVAL);
Blinker led3(LED3_PIN, LED3_INTERVAL);

void setup() {
    led1.begin();
    led2.begin();
    led3.begin();
}

void loop() {
    unsigned long currentMillis = millis();

    // Незалежне опитування та оновлення кожного світлодіода без блокування
    led1.update(currentMillis);
    led2.update(currentMillis);
    led3.update(currentMillis);
}