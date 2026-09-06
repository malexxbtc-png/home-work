#pragma once
#ifndef BLINKER_H
#define BLINKER_H

#include <Arduino.h>

class Blinker {
private:
    int pin;
    unsigned long interval;
    unsigned long previousMillis;
    volatile bool state; // Використовуємо volatile для збереження стану

public:
    Blinker(int p, unsigned long i);
    void begin();
    void update(unsigned long currentMillis);
};

#endif