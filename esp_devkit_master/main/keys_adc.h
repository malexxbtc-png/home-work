#ifndef KEYS_ADC_H
#define KEYS_ADC_H

#include "esp_err.h"
#include "keys_logic.h"

/* Ініціалізує ADC1 для піна PIN_KEYS_ADC (adc_oneshot + калібрування), завантажує калібрування з NVS
   (або значення за замовчуванням). */
esp_err_t keys_adc_init(void);

/* Усереднена напруга на піні, мВ. -1 при помилці. */
int keys_adc_read_mv(void);

/* Опитування: одна подія за виклик (викликати кожні ~10 мс). */
key_event_t keys_adc_poll(uint32_t now_ms);

key_id_t keys_adc_current(void);          /* яка кнопка зараз утримується */
int keys_adc_last_mv(void);
const key_cal_t *keys_adc_cal(void);

/* Застосувати нове калібрування (і зберегти в NVS, якщо save = true) */
esp_err_t keys_adc_set_cal(const key_cal_t *cal, bool save);

#endif
