#include "blinker.h"

Blinker::Blinker(int p, unsigned long i) {
    pin = p;
    interval = i;
    previousMillis = 0;
    state = LOW; // Початковий стан — вимкнено
}

void Blinker::begin() {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, state);
}

void Blinker::update(unsigned long currentMillis) {
    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        state = !state; // Змінюємо стан на протилежний
        digitalWrite(pin, state ? HIGH : LOW);
    }
}