#include "status.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

system_status_t status;
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;

void status_init(void)
{
    memset(&status, 0, sizeof(status));
    strcpy(status.last_snap_reason, "none");
    strcpy(status.last_snap_time, "--:--");
    strcpy(status.time_src, "none");
    status.wx_age = 999;
    status.cam_ok = status.sd_ok = status.tg_ok = -1;
    status.wifi_rssi = -127;
    status.last_snap_ok = -1;
    status.boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void status_log_error(const char *err)
{
    (void)err;
    status.err_count++;
}

uint32_t status_uptime_s(void)
{
    return ((uint32_t)(esp_timer_get_time() / 1000) - status.boot_ms) / 1000;
}

void status_set_alerts(const char *csv)
{
    portENTER_CRITICAL(&status_mux);
    strncpy(status.active_alerts, csv ? csv : "", sizeof(status.active_alerts) - 1);
    status.active_alerts[sizeof(status.active_alerts) - 1] = '\0';
    portEXIT_CRITICAL(&status_mux);
}

void status_snapshot(system_status_t *out)
{
    portENTER_CRITICAL(&status_mux);
    *out = status;
    portEXIT_CRITICAL(&status_mux);
}
