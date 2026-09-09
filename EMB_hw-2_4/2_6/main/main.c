#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "servo_system";

// Оптимізована конфігурація пінів для ESP32-S3
#define LDR_ADC_CHANNEL    ADC_CHANNEL_3 
#define SERVO_GPIO         GPIO_NUM_18    
#define GREEN_LED_GPIO     GPIO_NUM_5    
#define RED_LED_GPIO       GPIO_NUM_21    
#define YELLOW_LED_GPIO    GPIO_NUM_42   
#define BTN_GPIO           GPIO_NUM_40    

// Налаштування PWM (LEDC)
#define LEDC_MODE          LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES      LEDC_TIMER_13_BIT
#define MAX_DUTY           8191

#define SERVO_TIMER        LEDC_TIMER_0
#define SERVO_CHANNEL      LEDC_CHANNEL_0
#define SERVO_FREQ         50             // 50 Гц для сервоприводу

#define LED_TIMER          LEDC_TIMER_1
#define GREEN_CHANNEL      LEDC_CHANNEL_1
#define RED_CHANNEL        LEDC_CHANNEL_2
#define LED_FREQ           1000           // 1 кГц для світлодіодів

#define SERVO_MIN_DUTY     410   // ~1 мс (0°)
#define SERVO_MAX_DUTY     1024  // ~2.5 мс (180°)
#define DEBOUNCE_US        40000 // 40 мс фільтр антидребезгу

static adc_oneshot_unit_handle_t adc1_handle;
static esp_timer_handle_t debounce_timer;

static volatile bool auto_mode = true;         
static volatile bool force_close = false;      
static volatile int manual_state = 0;          
static int64_t press_start_time = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    (void)arg;
    esp_timer_stop(debounce_timer);
    esp_timer_start_once(debounce_timer, DEBOUNCE_US);
}

static void debounce_timer_cb(void *arg)
{
    (void)arg;
    int level = gpio_get_level(BTN_GPIO);

    if (level == 0) { 
        press_start_time = esp_timer_get_time();
    } else { 
        if (press_start_time > 0) {
            int64_t duration_ms = (esp_timer_get_time() - press_start_time) / 1000;
            press_start_time = 0;

            if (duration_ms >= 1500) {
                auto_mode = !auto_mode;
                if (auto_mode) {
                    force_close = false;
                    ESP_LOGW(TAG, "Режим: АВТО (Зелений LED)");
                } else {
                    manual_state = 0;
                    ESP_LOGW(TAG, "Режим: РУЧНИЙ (Червоний LED)");
                }
            } else if (duration_ms >= 50) {
                if (auto_mode) {
                    force_close = !force_close;
                    ESP_LOGI(TAG, "Примусове закриття: %s", force_close ? "АКТИВНО" : "ВИМКНЕНО");
                } else {
                    manual_state = (manual_state + 1) % 2;
                    ESP_LOGI(TAG, "Ручний стан: %s", manual_state == 1 ? "180°" : "0°");
                }
            }
        }
    }
}

static void hardware_init(void)
{
    // 1. Ініціалізація АЦП (LDR)
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, LDR_ADC_CHANNEL, &config));

    // 2. Ініціалізація GPIO кнопки (GPIO 43)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    // 3. Ініціалізація Жовтого LED (GPIO 42)
    gpio_config_t yellow_conf = {
        .pin_bit_mask = (1ULL << YELLOW_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLDOWN_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&yellow_conf));
    gpio_set_level(YELLOW_LED_GPIO, 0);

    // 4. Ініціалізація ШІМ Таймерів
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

    // 5. Ініціалізація ШІМ Каналів
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

    // 6. Налаштування таймера антидребезгу
    const esp_timer_create_args_t timer_args = {
        .callback = debounce_timer_cb,
        .name = "btn_debounce"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &debounce_timer));

    // 7. Сервіс переривань
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN_GPIO, gpio_isr_handler, NULL));
}

static void set_servo_angle(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t duty = SERVO_MIN_DUTY + ((SERVO_MAX_DUTY - SERVO_MIN_DUTY) * angle) / 180;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, SERVO_CHANNEL));
}

static void set_led_pwm(ledc_channel_t channel, uint32_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (MAX_DUTY * percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

static void control_task(void *arg)
{
    (void)arg;
    int ldr_raw = 0;
    int filtered_ldr = 2048; 
    int target_angle = 180;
    int current_angle = 180;
    TickType_t last_flash_tick = 0;
    bool yellow_flash_state = false;

    while (1) {
        if (auto_mode) {
            set_led_pwm(GREEN_CHANNEL, 100); 
            set_led_pwm(RED_CHANNEL, 0);     
        } else {
            set_led_pwm(GREEN_CHANNEL, 0);   
            set_led_pwm(RED_CHANNEL, 100);   
        }

        if (auto_mode) {
            if (force_close) {
                target_angle = 0;
            } else {
                if (adc_oneshot_read(adc1_handle, LDR_ADC_CHANNEL, &ldr_raw) == ESP_OK) {
                    filtered_ldr = (filtered_ldr * 7 + ldr_raw) / 8; 
                    target_angle = (filtered_ldr * 180) / 4095;      
                }
            }
        } else {
            target_angle = (manual_state == 1) ? 180 : 0;
        }

        if (current_angle != target_angle) {
            if (current_angle < target_angle) current_angle++;
            else current_angle--;

            set_servo_angle(current_angle);

            if ((xTaskGetTickCount() - last_flash_tick) >= pdMS_TO_TICKS(250)) {
                last_flash_tick = xTaskGetTickCount();
                yellow_flash_state = !yellow_flash_state;
                gpio_set_level(YELLOW_LED_GPIO, yellow_flash_state ? 1 : 0);
            }
        } else {
            yellow_flash_state = false;
            gpio_set_level(YELLOW_LED_GPIO, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    hardware_init();
    ESP_LOGI(TAG, "Система запущена з новими безпечними пінами.");
    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
}