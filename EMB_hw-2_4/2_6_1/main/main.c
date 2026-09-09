#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "servo_sys";

// Конфігурація пінів (ESP32-S3)
#define LDR_ADC_CHANNEL    ADC_CHANNEL_3  // Зазвичай GPIO4 на S3
#define SERVO_GPIO         GPIO_NUM_18    
#define GREEN_LED_GPIO     GPIO_NUM_5    
#define RED_LED_GPIO       GPIO_NUM_21    
#define YELLOW_LED_GPIO    GPIO_NUM_42   
#define BTN_GPIO           GPIO_NUM_40    

// Налаштування PWM
#define LEDC_MODE          LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES      LEDC_TIMER_13_BIT
#define MAX_DUTY           8191

#define SERVO_TIMER        LEDC_TIMER_0
#define SERVO_CHANNEL      LEDC_CHANNEL_0
#define SERVO_FREQ         50

#define LED_TIMER          LEDC_TIMER_1
#define GREEN_CHANNEL      LEDC_CHANNEL_1
#define RED_CHANNEL        LEDC_CHANNEL_2
#define LED_FREQ           1000

#define SERVO_MIN_DUTY     410   // ~1 мс (0°)
#define SERVO_MAX_DUTY     1024  // ~2.5 мс (180°)

typedef enum {
    EVENT_NONE,
    EVENT_BTN_SHORT_PRESS,
    EVENT_BTN_LONG_PRESS
} button_event_t;

typedef enum {
    SYS_STATE_AUTO,
    SYS_STATE_MANUAL_0,
    SYS_STATE_MANUAL_180
} system_state_t;

static adc_oneshot_unit_handle_t adc1_handle;
static QueueHandle_t button_event_queue;

static void hardware_init(void)
{
    // АЦП (LDR)
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, LDR_ADC_CHANNEL, &config));

    // Кнопка (без ISR)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    // Жовтий LED
    gpio_config_t yellow_conf = {
        .pin_bit_mask = (1ULL << YELLOW_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLDOWN_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&yellow_conf));
    gpio_set_level(YELLOW_LED_GPIO, 0);

    // ШІМ Таймери
    ledc_timer_config_t servo_timer = {
        .speed_mode = LEDC_MODE, .timer_num = SERVO_TIMER,
        .duty_resolution = LEDC_DUTY_RES, .freq_hz = SERVO_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&servo_timer));

    ledc_timer_config_t led_timer = {
        .speed_mode = LEDC_MODE, .timer_num = LED_TIMER,
        .duty_resolution = LEDC_DUTY_RES, .freq_hz = LED_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&led_timer));

    // ШІМ Канали
    ledc_channel_config_t servo_chan = {
        .speed_mode = LEDC_MODE, .channel = SERVO_CHANNEL, .timer_sel = SERVO_TIMER,
        .gpio_num = SERVO_GPIO, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&servo_chan));

    ledc_channel_config_t green_chan = {
        .speed_mode = LEDC_MODE, .channel = GREEN_CHANNEL, .timer_sel = LED_TIMER,
        .gpio_num = GREEN_LED_GPIO, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&green_chan));

    ledc_channel_config_t red_chan = {
        .speed_mode = LEDC_MODE, .channel = RED_CHANNEL, .timer_sel = LED_TIMER,
        .gpio_num = RED_LED_GPIO, .duty = 0, .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&red_chan));
}

static void set_servo_angle(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t duty = SERVO_MIN_DUTY + ((SERVO_MAX_DUTY - SERVO_MIN_DUTY) * angle) / 180;
    ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, SERVO_CHANNEL);
}

static void set_led_pwm(ledc_channel_t channel, uint32_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (MAX_DUTY * percent) / 100;
    ledc_set_duty(LEDC_MODE, channel, duty);
    ledc_update_duty(LEDC_MODE, channel);
}

// Функція анімації перемикання режимів
static void flash_transition(ledc_channel_t prev_mode_led)
{
    // Гасимо всі діоди перед анімацією
    set_led_pwm(GREEN_CHANNEL, 0);
    set_led_pwm(RED_CHANNEL, 0);
    
    for (int i = 0; i < 3; i++) {
        set_led_pwm(prev_mode_led, 100);
        gpio_set_level(YELLOW_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        
        set_led_pwm(prev_mode_led, 0);
        gpio_set_level(YELLOW_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

// Завдання кнопки (антидребезг)
static void button_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    int press_ticks = 0;
    bool long_press_sent = false;

    const int tick_ms = 20;
    const int debounce_ticks = 50 / tick_ms;     // 50 мс
    const int long_press_ticks = 2000 / tick_ms; // 2 секунди!

    while (1) {
        bool is_pressed = (gpio_get_level(BTN_GPIO) == 0);

        if (is_pressed) {
            press_ticks++;
            if (press_ticks >= long_press_ticks && !long_press_sent) {
                button_event_t ev = EVENT_BTN_LONG_PRESS;
                xQueueSend(button_event_queue, &ev, 0);
                long_press_sent = true; 
            }
        } else {
            if (press_ticks >= debounce_ticks && !long_press_sent) {
                button_event_t ev = EVENT_BTN_SHORT_PRESS;
                xQueueSend(button_event_queue, &ev, 0);
            }
            press_ticks = 0;
            long_press_sent = false;
        }
        vTaskDelay(pdMS_TO_TICKS(tick_ms));
    }
}

// Головний автомат керування (FSM)
static void control_task(void *arg)
{
    system_state_t current_state = SYS_STATE_AUTO;
    
    int ldr_raw = 0;
    int filtered_ldr = 2048; 
    int target_angle = 180;
    int current_angle = 180;
    
    TickType_t last_flash_tick = xTaskGetTickCount();
    bool yellow_flash_state = false;

    while (1) {
        button_event_t event;
        
        if (xQueueReceive(button_event_queue, &event, 0) == pdTRUE) {
            if (event == EVENT_BTN_LONG_PRESS) {
                if (current_state == SYS_STATE_AUTO) {
                    ESP_LOGI(TAG, "Перехід в РУЧНИЙ режим");
                    flash_transition(GREEN_CHANNEL);
                    current_state = SYS_STATE_MANUAL_180; // Відкрите положення за замовчуванням
                } else {
                    ESP_LOGI(TAG, "Перехід в АВТО режим");
                    flash_transition(RED_CHANNEL);
                    current_state = SYS_STATE_AUTO;
                }
            } else if (event == EVENT_BTN_SHORT_PRESS) {
                if (current_state == SYS_STATE_MANUAL_180) {
                    current_state = SYS_STATE_MANUAL_0;
                } else if (current_state == SYS_STATE_MANUAL_0) {
                    current_state = SYS_STATE_MANUAL_180;
                }
                // В режимі SYS_STATE_AUTO коротке натискання ігнорується
            }
        }

        // Логіка станів
        switch (current_state) {
            case SYS_STATE_AUTO:
                set_led_pwm(GREEN_CHANNEL, 100);
                set_led_pwm(RED_CHANNEL, 0);
                if (adc_oneshot_read(adc1_handle, LDR_ADC_CHANNEL, &ldr_raw) == ESP_OK) {
                    filtered_ldr = (filtered_ldr * 7 + ldr_raw) / 8; 
                    target_angle = (filtered_ldr * 180) / 4095;      
                }
                break;

            case SYS_STATE_MANUAL_0:
                set_led_pwm(GREEN_CHANNEL, 0);
                set_led_pwm(RED_CHANNEL, 100);
                target_angle = 0;
                break;

            case SYS_STATE_MANUAL_180:
                set_led_pwm(GREEN_CHANNEL, 0);
                set_led_pwm(RED_CHANNEL, 100);
                target_angle = 180;
                break;
        }

        // Рух сервоприводу та індикація Жовтим
        if (current_angle != target_angle) {
            if (current_angle < target_angle) current_angle++;
            else current_angle--;

            set_servo_angle(current_angle);

            // Блимання жовтого під час руху
            if ((xTaskGetTickCount() - last_flash_tick) >= pdMS_TO_TICKS(150)) {
                last_flash_tick = xTaskGetTickCount();
                yellow_flash_state = !yellow_flash_state;
                gpio_set_level(YELLOW_LED_GPIO, yellow_flash_state ? 1 : 0);
            }
        } else {
            // Зупинка блимання, коли ціль досягнута
            yellow_flash_state = false;
            gpio_set_level(YELLOW_LED_GPIO, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    button_event_queue = xQueueCreate(5, sizeof(button_event_t));
    hardware_init();
    ESP_LOGI(TAG, "Система запущена.");
    
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);
    xTaskCreate(control_task, "control_task", 4096, NULL, 4, NULL);
}