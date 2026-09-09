#ifndef APP_H
#define APP_H

#include "stm32f4xx_hal.h"

void setup(void);
void loop(void);

// Піни світлодіодів (GPIOB)
#define CAR_RED_PIN      GPIO_PIN_0
#define CAR_YELLOW_PIN   GPIO_PIN_1
#define CAR_GREEN_PIN    GPIO_PIN_10
#define CAR_PORT         GPIOB

#define PED_RED_PIN      GPIO_PIN_12
#define PED_GREEN_PIN    GPIO_PIN_13
#define PED_PORT         GPIOB

// Кнопка (GPIOA) та фоторезистор (ADC)
#define BTN_PIN          GPIO_PIN_0
#define BTN_PORT         GPIOA

#define LDR_PIN          GPIO_PIN_1
#define LDR_PORT         GPIOA

// Вбудований синій світлодіод (GPIOC)
#define HEARTBEAT_PIN    GPIO_PIN_13
#define HEARTBEAT_PORT   GPIOC

// Налаштування порогів темряви (0..4095)
// Якщо у вашій схемі в темряві значення зростає: DARK > 2500, LIGHT < 1500
#define LDR_DARK_THRESHOLD   2500 
#define LDR_LIGHT_THRESHOLD  1500

#endif // APP_H