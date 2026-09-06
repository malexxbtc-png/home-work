#include <Arduino.h>
#include "config.h"

// Змінні вимірювання затримки контакту реле
volatile unsigned long relayCmdTime = 0;
volatile unsigned long relayResponseTimeUs = 0;
volatile bool waitingForFeedback = false;
volatile bool newMeasurementReady = false;

// Обробник переривання: спрацьовує при замиканні контакту NO на GND (FALLING)
void IRAM_ATTR relayISR() {
    if (waitingForFeedback) {
        relayResponseTimeUs = micros() - relayCmdTime;
        waitingForFeedback = false;
        newMeasurementReady = true;
    }
}

// Функція установки ШІМ для двигуна
static void motorPwm(uint8_t duty8) {
    const uint32_t maxDuty = (1u << PWM_BITS) - 1u;
    ledcWrite(PWM_CHANNEL, (duty8 * maxDuty) / 255u);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    // Ініціалізація керування реле (Active LOW)
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    digitalWrite(RELAY_CTRL_PIN, HIGH); // Вимкнено за замовчуванням

    // Ініціалізація контакту зворотного зв'язку реле
    pinMode(RELAY_FB_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RELAY_FB_PIN), relayISR, FALLING);

    // Ініціалізація ШІМ двигуна на 1 піні
    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(MOTOR_PIN, PWM_CHANNEL);
    motorPwm(0);

    // Ініціалізація UART2 для логічного аналізатора (GPIO 45)
    Serial2.begin(9600, SERIAL_8N1, -1, UART_TX_PIN);

}

void loop() {
    // =================================================================
    // 1. ЦИКЛ ЗРОСТАННЯ ШІМ (0 -> 255)
    // =================================================================
  
    
    // Подаємо команду на увімкнення реле та засікаємо час
    relayCmdTime = micros();
    waitingForFeedback = true;
    digitalWrite(RELAY_CTRL_PIN, LOW); // Active LOW -> УВІМКНЕНО
    
    for (int d = 0; d <= 255; d += 5) {
        motorPwm((uint8_t)d);
        Serial2.write((uint8_t)d); // Сигнал на логічний аналізатор

        // Якщо переривання зафіксувало замикання контактів реле - виводимо час
        if (newMeasurementReady) {
            newMeasurementReady = false;
            Serial.printf("   [+] ЗАТРИМКА РЕЛЕ: %lu мкс (%.2f мс)\n", 
                          relayResponseTimeUs, relayResponseTimeUs / 1000.0);
        }

        int dutyPercent = (d * 100) / 255;
   

        delay(25); // Затримка кроку зростання
    }

    delay(400); // Пауза на максимумі потужності

    // =================================================================
    // 2. ЦИКЛ СПАДАННЯ ШІМ (255 -> 0)
    // =================================================================
    Serial.println("\n>>> СТАРТ ЦИКЛУ: Плавне спадання");
    
    // Вимикаємо реле
    waitingForFeedback = false;
    digitalWrite(RELAY_CTRL_PIN, HIGH); // Active LOW -> ВИМКНЕНО

    for (int d = 255; d >= 0; d -= 5) {
        motorPwm((uint8_t)d);
        Serial2.write((uint8_t)d); // Сигнал на логічний аналізатор

        int dutyPercent = (d * 100) / 255;
    

        delay(25); // Затримка кроку спадання
    }

    delay(800); // Пауза перед наступним циклом
}