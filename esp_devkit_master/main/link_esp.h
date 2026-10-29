#ifndef LINK_ESP_H
#define LINK_ESP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Рядок UART: <JSON>*<CRC16-CCITT, 4 hex>\n  (поліном 0x1021, початкове значення 0xFFFF) */

typedef struct {
    int cam;            /* 1 = камера ініціалізована */
    int sd;             /* 1 = SD змонтована */
    int tg;
    int64_t rx_us;
    bool valid;
} link_hb_t;

typedef struct {
    bool ok;
    char file[48];      /* відносний шлях знімка на SD, напр. 20261025/194512_pir.jpg */
    char err[24];
} link_snap_done_t;

void link_esp_init(void);
void link_esp_send_json(const char *json_str);

/* Останній heartbeat від ESP32-S3, якщо він не старший за max_age_ms */
bool link_esp_get_hb(link_hb_t *out, uint32_t max_age_ms);

/* Забирає повідомлення "snap_done" (один раз) */
bool link_esp_take_snap_done(link_snap_done_t *out);

bool link_esp_is_online(uint32_t timeout_ms);

#endif
