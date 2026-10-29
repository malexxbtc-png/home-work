#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* STM32 / датчик */
    bool stm_ok;
    float t, rh, p, td;
    int stm_fail;

    /* ESP32-S3 (дані з heartbeat; -1 = невідомо) */
    bool esp_online;
    int cam_ok;
    int sd_ok;
    int tg_ok;
    int wifi_rssi;
    int wx_age;                 /* хвилини, 999 = немає даних */

    /* останній знімок */
    char last_snap_reason[16];
    char last_snap_time[16];
    char last_snap_file[48];
    int last_snap_ok;           /* -1 ще не було, 0 помилка, 1 успіх */

    char time_src[8];           /* none | rtc | ntp */
    bool rtc_ok;

    bool pir_state;
    char active_alerts[128];    /* через кому: wind,fog,... */

    int err_count;
    uint32_t boot_ms;
} system_status_t;

extern system_status_t status;

void status_init(void);
void status_log_error(const char *err);
uint32_t status_uptime_s(void);

/* Потокобезпечні допоміжні функції */
void status_set_alerts(const char *csv);
void status_snapshot(system_status_t *out);

#endif
