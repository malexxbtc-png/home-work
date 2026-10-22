#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool stm_ok;
    float t;
    float rh;
    float p;
    float td;
    uint8_t stm_hb;
    uint32_t stm_hb_t;
    int stm_fail;

    bool esp_online;
    int cam_ok;
    int sd_ok;
    int tg_ok;
    int wifi_rssi;
    int wx_age;

    char last_snap_reason[16];
    char last_snap_time[8];
    char time_src[8];

    bool pir_state;
    char active_alerts[128];

    int err_count;
    uint32_t boot_ms;
} system_status_t;

extern system_status_t status;

void status_init(void);
void status_log_error(const char *err);
uint32_t status_uptime_s(void);

#endif