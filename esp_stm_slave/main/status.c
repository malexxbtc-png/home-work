#include "status.h"
#include "esp_timer.h"
#include <string.h>

system_status_t status;

void status_init(void) {
    memset(&status, 0, sizeof(system_status_t));
    strcpy(status.last_snap_reason, "none");
    strcpy(status.last_snap_time, "--:--");
    strcpy(status.time_src, "none");
    status.wx_age = 999;
    status.boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void status_log_error(const char *err) {
    status.err_count++;
}

uint32_t status_uptime_s(void) {
    return ((uint32_t)(esp_timer_get_time() / 1000) - status.boot_ms) / 1000;
}