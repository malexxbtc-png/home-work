#include "buzzer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

typedef struct {
    int melody_id;
    uint32_t duration_sec; // 0 = зіграти один раз, >0 = зациклити на N секунд
} MelodyCmd;

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

    melody_queue = xQueueCreate(1, sizeof(MelodyCmd));
    xTaskCreate(melody_task, "melody_task", 3072, NULL, 5, NULL);
}

void buzzer_play_tone(int frequency, int duration_ms) {
    if (frequency == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    } else {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
        // 4096 це рівно 50% від 8191 (13-бітний таймер) — забезпечує максимальну гучність
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 4096);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    }
    
    // Збільшуємо тривалість ноти в 1.5 рази, щоб мелодії звучали довше і плавніше
    vTaskDelay(pdMS_TO_TICKS(duration_ms * 1.5));
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
    vTaskDelay(pdMS_TO_TICKS(30)); // Коротка пауза між нотами
}

static void melody_task(void *pvParameters) {
    MelodyCmd cmd;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(melody_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.melody_id >= 1 && cmd.melody_id <= PLAYLIST_SIZE) {
                const Melody* m = &custom_playlist[cmd.melody_id - 1];
                
                TickType_t start_tick = xTaskGetTickCount();
                TickType_t max_ticks = (cmd.duration_sec > 0) ? pdMS_TO_TICKS(cmd.duration_sec * 1000) : 0;

                int note_idx = 0;
                while (1) {
                    MelodyCmd next_cmd;
                    // Перериваємо програвання, якщо з'явилася нова команда в черзі
                    if (xQueuePeek(melody_queue, &next_cmd, 0) == pdTRUE) {
                        break;
                    }

                    // Перевіряємо ліміт часу (якщо задана тривалість у секундах)
                    if (cmd.duration_sec > 0 && (xTaskGetTickCount() - start_tick) >= max_ticks) {
                        break;
                    }

                    buzzer_play_tone(m->notes[note_idx].frequency, m->notes[note_idx].duration_ms);

                    note_idx++;
                    if (note_idx >= m->length) {
                        if (cmd.duration_sec > 0) {
                            note_idx = 0; // Повторюємо мелодію по колу, поки не вийде час
                        } else {
                            break; // Зіграли один раз і виходимо
                        }
                    }
                }
            }
            // Глушимо зумер після завершення
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7);
        }
    }
}

void buzzer_play_melody_async(int melody_id) {
    if (melody_queue != NULL) {
        MelodyCmd cmd = {.melody_id = melody_id, .duration_sec = 0};
        xQueueOverwrite(melody_queue, &cmd);
    }
}

void buzzer_play_melody_loop_async(int melody_id, uint32_t duration_sec) {
    if (melody_queue != NULL) {
        MelodyCmd cmd = {.melody_id = melody_id, .duration_sec = duration_sec};
        xQueueOverwrite(melody_queue, &cmd);
    }
}