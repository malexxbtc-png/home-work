#include "app.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    STATE_CAR_GREEN,       // Зелений авто (9 сек)
    STATE_CAR_WARNING,     // Блимання жовтого (1 сек)
    STATE_CAR_YELLOW,      // Жовтий авто (2 сек)
    STATE_PED_GREEN,       // Зелений пішоходу (3 сек)
    STATE_CAR_TRANSITION,  // Перехідний жовтий (1 сек)
    STATE_NIGHT_MODE       // Нічний режим: блимає тільки жовтий
} TrafficState;

static TrafficState currentState = STATE_CAR_GREEN;
static TrafficState previousDayState = STATE_CAR_GREEN; 
static uint32_t lastStateTime = 0;
static uint32_t blinkTimer = 0;
static uint32_t ldrCheckTimer = 0;
static uint32_t heartbeatTimer = 0;
static uint32_t telemetryTimer = 0;
static uint8_t yellowState = 0;
static uint8_t stateEntered = 0;

volatile uint8_t pedestrianButtonPressed = 0;
static uint32_t lastPressTick = 0;
static uint8_t isNightMode = 0;

extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart2;

void sendLog(const char* message) {
    HAL_UART_Transmit(&huart2, (uint8_t*)message, strlen(message), 100);
}

void setLights(uint8_t carG, uint8_t carY, uint8_t carR, uint8_t pedG, uint8_t pedR) {
    HAL_GPIO_WritePin(CAR_PORT, CAR_GREEN_PIN, carG ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CAR_PORT, CAR_YELLOW_PIN, carY ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CAR_PORT, CAR_RED_PIN, carR ? GPIO_PIN_SET : GPIO_PIN_RESET);
    
    HAL_GPIO_WritePin(PED_PORT, PED_GREEN_PIN, pedG ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PED_PORT, PED_RED_PIN, pedR ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void setup(void) {
    uint32_t now = HAL_GetTick();
    lastStateTime = now;
    ldrCheckTimer = now;
    heartbeatTimer = now;
    telemetryTimer = now;
    stateEntered = 0;
    
    setLights(1, 0, 0, 0, 1);
    sendLog("\r\n=== System Started: FSM Traffic Light ===\r\n");
}

void loop(void) {
    uint32_t currentTime = HAL_GetTick();
    
    // 1. Індикатор роботи (Heartbeat на синьому світлодіоді PC13)
    if (currentTime - heartbeatTimer >= 500) {
        heartbeatTimer = currentTime;
        HAL_GPIO_TogglePin(HEARTBEAT_PORT, HEARTBEAT_PIN);
    }
    
    // 2. Опитування АЦП та логування значення LDR
    if (currentTime - ldrCheckTimer >= 1000) {
        ldrCheckTimer = currentTime;
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            uint32_t lightLevel = HAL_ADC_GetValue(&hadc1);
            
            if (currentTime - telemetryTimer >= 2000) {
                telemetryTimer = currentTime;
                char msg[64];
                snprintf(msg, sizeof(msg), "LOG: ADC LDR = %lu | Mode = %s\r\n", 
                         lightLevel, isNightMode ? "NIGHT" : "DAY");
                sendLog(msg);
            }

            if (lightLevel > LDR_DARK_THRESHOLD && !isNightMode) {
                isNightMode = 1;
                previousDayState = currentState;
                currentState = STATE_NIGHT_MODE;
                stateEntered = 0;
                sendLog("-> EVENT: Night Mode Switched (Low Light)\r\n");
            } 
            else if (lightLevel < LDR_LIGHT_THRESHOLD && isNightMode) {
                isNightMode = 0;
                currentState = previousDayState;
                stateEntered = 0;
                sendLog("-> EVENT: Day Mode Restored (Daylight)\r\n");
            }
        }
        HAL_ADC_Stop(&hadc1);
    }

    // 3. Управління виходами під час входу в стан
    if (!stateEntered) {
        switch (currentState) {
            case STATE_CAR_GREEN:
                setLights(1, 0, 0, 0, 1);
                break;
            case STATE_CAR_WARNING:
                setLights(0, 0, 0, 0, 1);
                blinkTimer = currentTime;
                yellowState = 0;
                break;
            case STATE_CAR_YELLOW:
                setLights(0, 1, 0, 0, 1);
                break;
            case STATE_PED_GREEN:
                setLights(0, 0, 1, 1, 0);
                sendLog("-> EVENT: Pedestrian Green Active\r\n");
                break;
            case STATE_CAR_TRANSITION:
                setLights(0, 1, 0, 0, 1);
                break;
            case STATE_NIGHT_MODE:
                setLights(0, 0, 0, 0, 0);
                blinkTimer = currentTime;
                yellowState = 0;
                break;
        }
        stateEntered = 1;
    }

    // 4. Головний автомат станів (FSM)
    switch (currentState) {
        case STATE_CAR_GREEN:
            if ((currentTime - lastStateTime >= 9000) || pedestrianButtonPressed) {
                if (pedestrianButtonPressed) {
                    sendLog("-> EVENT: Pedestrian Request Accepted\r\n");
                }
                pedestrianButtonPressed = 0;
                currentState = STATE_CAR_WARNING;
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;

        case STATE_CAR_WARNING:
            if (currentTime - blinkTimer >= 200) {
                yellowState = !yellowState;
                HAL_GPIO_WritePin(CAR_PORT, CAR_YELLOW_PIN, yellowState ? GPIO_PIN_SET : GPIO_PIN_RESET);
                blinkTimer = currentTime;
            }
            if (currentTime - lastStateTime >= 1000) {
                currentState = STATE_CAR_YELLOW;
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;

        case STATE_CAR_YELLOW:
            if (currentTime - lastStateTime >= 2000) {
                currentState = STATE_PED_GREEN;
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;

        case STATE_PED_GREEN:
            if (currentTime - lastStateTime >= 3000) {
                currentState = STATE_CAR_TRANSITION;
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;

        case STATE_CAR_TRANSITION:
            if (currentTime - lastStateTime >= 1000) {
                // Якщо йде нічний режим — повертаємося до блимання жовтого, інакше в денний зелений
                if (isNightMode) {
                    currentState = STATE_NIGHT_MODE;
                } else {
                    currentState = STATE_CAR_GREEN;
                }
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;

        case STATE_NIGHT_MODE:
            // Блимання жовтого світлофора для авто
            if (currentTime - blinkTimer >= 500) {
                yellowState = !yellowState;
                HAL_GPIO_WritePin(CAR_PORT, CAR_YELLOW_PIN, yellowState ? GPIO_PIN_SET : GPIO_PIN_RESET);
                blinkTimer = currentTime;
            }
            // Реакція на кнопку в нічному режимі: запуск безпечного переходу для пішохода
            if (pedestrianButtonPressed) {
                pedestrianButtonPressed = 0;
                sendLog("-> EVENT: Pedestrian Request in Night Mode\r\n");
                currentState = STATE_CAR_YELLOW;
                stateEntered = 0;
                lastStateTime = currentTime;
            }
            break;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == BTN_PIN) {
        uint32_t currentTick = HAL_GetTick();
        if (currentTick - lastPressTick > 300) {
            pedestrianButtonPressed = 1;
            lastPressTick = currentTick;
        }
    }
}