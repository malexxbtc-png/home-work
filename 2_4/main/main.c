#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button-hw-debounce";

#define LED_GPIO GPIO_NUM_21
#define BTN_GPIO GPIO_NUM_1
#define DEBOUNCE_MS 30
#define RELEASE_GUARD_MS 30

static atomic_bool btn_irq_pending = false;
static bool led_on = false;
static uint32_t press_count = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    (void)arg;
    atomic_store(&btn_irq_pending, true);
}

static void setup_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
}

static void setup_button_irq(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Фронт падіння (натиск до GND)
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_GPIO, gpio_isr_handler, NULL));
}

static void print_assignment_table(void)
{
    ESP_LOGI(TAG, "==========================================================================================");
    ESP_LOGI(TAG, "                      ПОРІВНЯЛЬНА ТАБЛИЦЯ МЕТОДІВ УСУНЕННЯ БРЯЗКОТУ                        ");
    ESP_LOGI(TAG, "==========================================================================================");
    ESP_LOGI(TAG, " Метод         | Кількість хибних спрацювань      | Затримка      | Складність | Коментар");
    ESP_LOGI(TAG, "------------------------------------------------------------------------------------------");
    ESP_LOGI(TAG, " Без debounce  | Висока (множинні імпульси)       | Мінімальна    | Низька     | Дребезг контактів");
    ESP_LOGI(TAG, " Time-based    | Середня (реагує на відпускання)  | Низька (~50мс)| Низька     | Часовий фільтр");
    ESP_LOGI(TAG, " State-based   | Низька (повне блокування)        | Середня(~40мс)| Середня    | Перевірка рівня");
    ESP_LOGI(TAG, " Polling       | Нульова                          | Середня(5-10м)| Низька     | Опитування в цикли");
    ESP_LOGI(TAG, " Hardware RC   | Мінімальна / Нульова             | Мала          | Вища       | RC-фільтр (100nF+100R)");
    ESP_LOGI(TAG, "==========================================================================================");
}

static void button_task(void *arg)
{
    (void)arg;
    while (1) {
        bool expected = true;
        if (atomic_compare_exchange_strong(&btn_irq_pending, &expected, false)) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            
            // Перевірка поточного стану пін-коду (фільтрація шуму)
            if (gpio_get_level(BTN_GPIO) == 0) {
                led_on = !led_on;
                gpio_set_level(LED_GPIO, led_on ? 1 : 0);
                press_count++;
                ESP_LOGI(TAG, "Press #%lu detected, LED -> %s", (unsigned long)press_count, led_on ? "ON" : "OFF");

                // Очікування відпускання кнопки
                while (gpio_get_level(BTN_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                
                vTaskDelay(pdMS_TO_TICKS(RELEASE_GUARD_MS));
            }
            atomic_store(&btn_irq_pending, false);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    setup_led();
    setup_button_irq();
    
    // Виведення таблиці із завдань в логи при запуску
    print_assignment_table();
    
    ESP_LOGI(TAG, "System ready. BTN: GPIO%d, LED: GPIO%d", (int)BTN_GPIO, (int)LED_GPIO);

    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);

    // Запобігання завершенню main_task
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}