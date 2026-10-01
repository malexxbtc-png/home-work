#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static QueueHandle_t melody_queue = NULL;

static void melody_task(void *pvParameters);

void buzzer_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .freq_hz          = 2000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_7,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ledc_channel_config(&channel);

    melody_queue = xQueueCreate(1, sizeof(int));
    xTaskCreate(melody_task, "melody_task", 3072, NULL, 5, NULL);
}

void buzzer_play_tone(int frequency, int duration_ms) {
    if (frequency == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 4096);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    vTaskDelay(pdMS_TO_TICKS(20)); // Пауза між нотами
}

static void melody_task(void *pvParameters) {
    int melody_id;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(melody_queue, &melody_id, portMAX_DELAY) == pdTRUE) {
            // melody_id == 0 означає "Стоп"
            if (melody_id >= 1 && melody_id <= 4) {
                const Melody* m = &classic_playlist[melody_id - 1];
                for (int i = 0; i < m->length; i++) {
                    int next_melody;
                    // Перериваємо програвання, якщо в черзі з'явилася нова команда
                    if (xQueuePeek(melody_queue, &next_melody, 0) == pdTRUE) {
                        break; 
                    }
                    buzzer_play_tone(m->notes[i].frequency, m->notes[i].duration_ms);
                }
            }
            // Гарантовано глушимо зумер після завершення або при командах Стоп / Переривання
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
        }
    }
}

void buzzer_play_melody_async(int melody_id) {
    if (melody_queue != NULL) {
        xQueueOverwrite(melody_queue, &melody_id);
    }
}