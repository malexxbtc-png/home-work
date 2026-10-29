#ifndef LINK_PICO_H
#define LINK_PICO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Протокол (115200 8N1): рядок = <JSON>*<CRC16-CCITT, 4 hex>\n
   CRC: поліном 0x1021, початкове значення 0xFFFF, по байтах JSON. */

void link_pico_init(void);

/* Надсилає JSON-повідомлення (безпечно викликати з різних задач). true = передано. */
bool link_pico_send_json(const char *json_str);

/* Імпульс на лінії IRQ до майстра (необов'язковий) */
void link_pico_trigger_irq(void);

/* Останнє повідомлення "data" від майстра. age_ms може бути NULL. */
bool link_pico_get_latest_data(char *dest, size_t max_len, uint32_t *age_ms);

/* Забирає останній запит "snap" від майстра (після цього він видаляється) */
bool link_pico_get_latest_snap(char *dest, size_t max_len);

/* Забирає останнє повідомлення "wx" від майстра (один раз) */
bool link_pico_take_wx(char *dest, size_t max_len);

/* Забирає останнє повідомлення "time" (один раз). epoch_local = місцевий Unix-час, доведений
   до моменту виклику (додано час, що минув після прийому). */
bool link_pico_take_time(int64_t *epoch_local);

/* true, якщо "data" від майстра приходили за останні WEATHER_LINK_PICO_DATA_STALE_TIMEOUT_MS */
bool link_pico_data_is_fresh(void);

#endif /* LINK_PICO_H */
