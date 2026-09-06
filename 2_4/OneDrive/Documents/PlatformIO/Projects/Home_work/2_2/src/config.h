#pragma once

// Піни периферії
#ifndef MOTOR_PIN
#define MOTOR_PIN 1        // Вихід ШІМ на транзистор двигуна
#endif

#ifndef RELAY_CTRL_PIN
#define RELAY_CTRL_PIN 19  // Вихід керування реле (Active LOW)
#endif

#ifndef RELAY_FB_PIN
#define RELAY_FB_PIN 21    // Пін зчитування стану контакту реле (NO) для переривання
#endif

#ifndef UART_TX_PIN
#define UART_TX_PIN 45     // Вихід UART2 TX для логічного аналізатора
#endif

// Налаштування апаратного ШІМ
#ifndef PWM_FREQ_HZ
#define PWM_FREQ_HZ 10000
#endif

#ifndef PWM_BITS
#define PWM_BITS 8
#endif

#ifndef PWM_CHANNEL
#define PWM_CHANNEL 0
#endif