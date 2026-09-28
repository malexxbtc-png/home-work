#include "stdio.h"
#include "string.h"
#include "esp_system.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "PWM_AUTO_RAMP";

// --- Піни ---
#define MOTOR_GPIO      GPIO_NUM_22
#define LED_GPIO        GPIO_NUM_23

// --- Налаштування ШІМ (5 кГц) ---
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_RES         LEDC_TIMER_8_BIT // Діапазон: 0 - 255
#define PWM_FREQ_HZ     5000             // 5 кГц
#define MOTOR_PWM_CHAN  LEDC_CHANNEL_0
#define LED_PWM_CHAN    LEDC_CHANNEL_1

#define MOTOR_MIN_DUTY  35               // Поріг старту мотора (0-255)
#define RAMP_STEP_MS    40               // Швидкість розгону (затримка між кроками, мс)

static void setup_hardware(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num = PWM_TIMER,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t motor_channel = {
        .speed_mode = PWM_MODE,
        .channel = MOTOR_PWM_CHAN,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = MOTOR_GPIO,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&motor_channel));

    ledc_channel_config_t led_channel = {
        .speed_mode = PWM_MODE,
        .channel = LED_PWM_CHAN,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LED_GPIO,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&led_channel));
}

void app_main(void) {
    setup_hardware();
    ESP_LOGI(TAG, "Автоматичний цикл запущено. Логування кожну чверть (25%%).");

    while (1) {
        // Плавно збільшуємо наповнення від 0 до 255
        for (uint32_t duty = 0; duty <= 255; duty++) {
            
            // 1. Оновлюємо світлодіод
            ledc_set_duty(PWM_MODE, LED_PWM_CHAN, duty);
            ledc_update_duty(PWM_MODE, LED_PWM_CHAN);

            // 2. Оновлюємо мотор (з урахуванням мінімального порогу старту)
            uint32_t motor_duty = duty;
            if (motor_duty > 0 && motor_duty < MOTOR_MIN_DUTY) {
                motor_duty = MOTOR_MIN_DUTY;
            }
            ledc_set_duty(PWM_MODE, MOTOR_PWM_CHAN, motor_duty);
            ledc_update_duty(PWM_MODE, MOTOR_PWM_CHAN);

            // 3. Менше логувати: виводимо інфо ТІЛЬКИ коли збільшено на чверть (4 етапи)
            if (duty == 0) {
                ESP_LOGI(TAG, "ШІМ:   0%% (0/255)");
            } else if (duty == 64) {
                ESP_LOGI(TAG, "ШІМ:  25%% (64/255) - Чверть 1");
            } else if (duty == 128) {
                ESP_LOGI(TAG, "ШІМ:  50%% (128/255) - Чверть 2");
            } else if (duty == 192) {
                ESP_LOGI(TAG, "ШІМ:  75%% (192/255) - Чверть 3");
            } else if (duty == 255) {
                ESP_LOGI(TAG, "ШІМ: 100%% (255/255) - Максимум!");
            }

            // Затримка між кроками розгону
            vTaskDelay(pdMS_TO_TICKS(RAMP_STEP_MS));
        }

        // --- Перерва між циклами ---
        ESP_LOGW(TAG, "Цикл завершено. Скидання в 0 та пауза 1.5 сек...");
        
        // Повністю вимикаємо мотор і діод на час паузи
        ledc_set_duty(PWM_MODE, MOTOR_PWM_CHAN, 0);
        ledc_update_duty(PWM_MODE, MOTOR_PWM_CHAN);
        ledc_set_duty(PWM_MODE, LED_PWM_CHAN, 0);
        ledc_update_duty(PWM_MODE, LED_PWM_CHAN);

        // Чекаємо перед початком нового циклу розгону
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}