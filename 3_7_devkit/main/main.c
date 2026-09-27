#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pwm_hw.h"

static const char *TAG = "SERVO_180_LOGIC";

// Піни енкодера
#define ENC_CLK_GPIO      GPIO_NUM_25
#define ENC_DT_GPIO       GPIO_NUM_26
#define ENC_SW_GPIO       GPIO_NUM_27

#define SUBSTEPS_PER_CLICK 4 
#define REVERSE_ENCODER_DIR 0 

// 4 світлодіоди індикації секторів (для 180°, розділених на 4 частини по 45°)
// Сектор 1: 0-45°, Сектор 2: 45-90°, Сектор 3: 90-135°, Сектор 4: 135-180°
#define LED_SEC_1_GPIO    GPIO_NUM_16
#define LED_SEC_2_GPIO    GPIO_NUM_17
#define LED_SEC_3_GPIO    GPIO_NUM_18
#define LED_SEC_4_GPIO    GPIO_NUM_19

// Пін статусу серво (блимає при досягненні меж 0° або 180° під час спроби йти далі)
#define LED_STATUS_GPIO   GPIO_NUM_21

// Пін сервопривода
#define SERVO_GPIO        GPIO_NUM_5

#define US_AT_0           500
#define US_AT_180         2400

// Максимальна кількість кроків для 180 градусів (наприклад, 40 кліків енкодера)
#define MAX_SERVO_STEPS   40 

static volatile int g_current_step = 0;        
static volatile bool g_position_changed = false; 
static volatile int g_direction = 0; // +1 = рух вгору, -1 = рух вниз
static volatile bool g_limit_hit = false; // Прапорець досягнення межі (0 або 180)

static const int8_t enc_table[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0
};

static volatile uint8_t g_prev_enc_state = 0;
static volatile int8_t g_enc_substeps = 0;

static void IRAM_ATTR encoder_isr_handler(void *arg) {
    uint8_t clk = gpio_get_level(ENC_CLK_GPIO);
    uint8_t dt  = gpio_get_level(ENC_DT_GPIO);

    uint8_t curr_state = (clk << 1) | dt;
    uint8_t index = (g_prev_enc_state << 2) | curr_state;
    int8_t step = enc_table[index];

    if (step != 0) {
        g_prev_enc_state = curr_state;

#if REVERSE_ENCODER_DIR
        step = -step;
#endif

        g_enc_substeps += step;

        if (g_enc_substeps >= SUBSTEPS_PER_CLICK) {
            g_direction = 1; // Команда на збільшення (вгору до 180°)
            g_enc_substeps = 0;
            
            if (g_current_step < MAX_SERVO_STEPS) {
                g_current_step++;
                g_limit_hit = false;
            } else {
                g_limit_hit = true; // Досягнули верхньої межі (180°)
            }
            g_position_changed = true;

        } else if (g_enc_substeps <= -SUBSTEPS_PER_CLICK) {
            g_direction = -1; // Команда на зменшення (вниз до 0°)
            g_enc_substeps = 0;
            
            if (g_current_step > 0) {
                g_current_step--;
                g_limit_hit = false;
            } else {
                g_limit_hit = true; // Досягнули нижньої межі (0°)
            }
            g_position_changed = true;
        }
    }
}

static uint32_t step_to_pulse_us(int step, int *out_deg) {
    // Мапимо кроки безпосередньо у діапазон 0° - 180°
    int deg = (step * 180) / MAX_SERVO_STEPS;
    *out_deg = deg;
    return US_AT_0 + ((uint32_t)deg * (US_AT_180 - US_AT_0)) / 180;
}

static void setup_hardware(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_SEC_1_GPIO) | (1ULL << LED_SEC_2_GPIO) | 
                        (1ULL << LED_SEC_3_GPIO) | (1ULL << LED_SEC_4_GPIO) | 
                        (1ULL << LED_STATUS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);

    gpio_config_t enc_conf = {
        .pin_bit_mask = (1ULL << ENC_CLK_GPIO) | (1ULL << ENC_DT_GPIO) | (1ULL << ENC_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&enc_conf);

    uint8_t clk = gpio_get_level(ENC_CLK_GPIO);
    uint8_t dt  = gpio_get_level(ENC_DT_GPIO);
    g_prev_enc_state = (clk << 1) | dt;

    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(ENC_CLK_GPIO, encoder_isr_handler, NULL);
    gpio_isr_handler_add(ENC_DT_GPIO,  encoder_isr_handler, NULL);

    pwm_hw_init(SERVO_GPIO);
    pwm_hw_set_pulse_us(US_AT_0);

    ESP_LOGI(TAG, "Систему налаштовано: 180° серво, 4 сектори, логіка меж та індикація.");
}

static void control_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20); 

    while (1) {
        int current_deg = 0;
        uint32_t us = step_to_pulse_us(g_current_step, &current_deg);

        if (g_position_changed) {
            g_position_changed = false;
            pwm_hw_set_pulse_us(us);
            ESP_LOGI(TAG, "Кут: %3d° | Напрямок: %s | Межа: %s", 
                     current_deg, (g_direction > 0) ? "Вгору (+)" : "Вниз (-)", 
                     g_limit_hit ? "ТАК (Упир у край)" : "НІ");
        }

        // --- Визначення активного сектора (4 сектори по 45°) ---
        // Сектор 1: 0-45°, Сектор 2: 45-90°, Сектор 3: 90-135°, Сектор 4: 135-180°
        int active_sector = 0;
        if      (current_deg <= 45)  active_sector = 1;
        else if (current_deg <= 90)  active_sector = 2;
        else if (current_deg <= 135) active_sector = 3;
        else                         active_sector = 4;

        // Ефект блимання для цільового/рухомого сектору (швидкість блимання ~250 мс)
        int blink_state = (xTaskGetTickCount() / 250) % 2;

        // Логіка горіння світлодіодів секторів:
        // - Пройдені сектори горять постійно (1)
        // - Цільовий сектор, куди рухаємось, блимає
        bool led1_val = 0, led2_val = 0, led3_val = 0, led4_val = 0;

        if (g_direction > 0) {
            // Рух вгору (збільшення кута)
            led1_val = (active_sector > 1) ? 1 : (active_sector == 1 ? blink_state : 0);
            led2_val = (active_sector > 2) ? 1 : (active_sector == 2 ? blink_state : 0);
            led3_val = (active_sector > 3) ? 1 : (active_sector == 3 ? blink_state : 0);
            led4_val = (active_sector == 4) ? blink_state : 0;
        } else if (g_direction < 0) {
            // Рух вниз (зменшення кута)
            led1_val = (active_sector == 1) ? blink_state : 0;
            led2_val = (active_sector < 2) ? 0 : (active_sector == 2 ? blink_state : 1);
            led3_val = (active_sector < 3) ? 0 : (active_sector == 3 ? blink_state : 1);
            led4_val = (active_sector < 4) ? 0 : (active_sector == 4 ? blink_state : 1);
        } else {
            // У спокої — просто світиться поточний сектор
            led1_val = (active_sector == 1);
            led2_val = (active_sector == 2);
            led3_val = (active_sector == 3);
            led4_val = (active_sector == 4);
        }

        gpio_set_level(LED_SEC_1_GPIO, led1_val);
        gpio_set_level(LED_SEC_2_GPIO, led2_val);
        gpio_set_level(LED_SEC_3_GPIO, led3_val);
        gpio_set_level(LED_SEC_4_GPIO, led4_val);

        // --- Індикатор статусу серво ---
        // Якщо досягнуто межі (0° або 180°) і користувач продовжує крутити туди ж — статусний LED інтенсивно блимає
        if (g_limit_hit) {
            gpio_set_level(LED_STATUS_GPIO, (xTaskGetTickCount() / 100) % 2);
        } else {
            gpio_set_level(LED_STATUS_GPIO, 0); // Вимкнений у звичайному стані
        }

        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

void app_main(void) {
    setup_hardware();
    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
}