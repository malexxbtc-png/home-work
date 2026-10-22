#ifndef LINK_ESP_H
#define LINK_ESP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void link_esp_init(void);
void link_esp_send_json(const char *json_str);

/* Останнє повідомлення "wx" (погода) від ESP32-S3 */
bool link_esp_get_latest_wx(char *dest, size_t max_len);

/* Забирає останнє повідомлення "time" від ESP32-S3 (один раз).
   epoch_local = Unix-час МІСЦЕВОГО годинника (UTC + зсув з літнім часом),
   уже доведений до моменту виклику (додано час, що минув після прийому). */
bool link_esp_take_time(int64_t *epoch_local);

bool link_esp_is_online(uint32_t timeout_ms);

#endif
