#include "triggers.h"

#include <stdio.h>
#include <string.h>
#include "clock.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events.h"
#include "link_esp.h"
#include "status.h"

static const char *TAG = "triggers";

static uint32_t pending_seq;            /* 0 = немає очікуваного знімка */
static char pending_reason[16];
static int64_t pending_deadline_us;
static int64_t last_request_us = INT64_MIN / 2;

void triggers_init(void)
{
    pending_seq = 0;
    last_request_us = INT64_MIN / 2;
}

bool triggers_photo_pending(void)
{
    return pending_seq != 0;
}

static bool send_snap_request(uint32_t seq, const char *reason)
{
    char json[96];
    int n = snprintf(json, sizeof(json), "{\"t\":\"snap\",\"reason\":\"%s\",\"seq\":%u}",
                     reason, (unsigned)seq);
    if (n <= 0 || (size_t)n >= sizeof(json)) {
        return false;
    }
    link_esp_send_json(json);
    return true;
}

/* Перевіряє, чи можна зараз просити знімок. manual ігнорує кулдаун. */
static snap_req_t photo_gate(bool manual)
{
    if (!link_esp_is_online(ESP_OFFLINE_S * 1000)) {
        return SNAP_REQ_OFFLINE;
    }
    if (pending_seq != 0) {
        return SNAP_REQ_BUSY;
    }
    if (!manual && (esp_timer_get_time() - last_request_us) < (int64_t)ALERT_SNAP_COOLDOWN_S * 1000000) {
        return SNAP_REQ_COOLDOWN;
    }
    return SNAP_REQ_SENT;
}

static void start_request(uint32_t seq, const char *reason)
{
    snprintf(pending_reason, sizeof(pending_reason), "%s", reason);
    pending_seq = seq;
    pending_deadline_us = esp_timer_get_time() + (int64_t)SNAP_TIMEOUT_S * 1000000;
    last_request_us = esp_timer_get_time();
    send_snap_request(seq, reason);
}

uint32_t trigger_event(const char *type, const char *info, bool want_photo)
{
    char stamp[20];
    clock_stamp(stamp, sizeof(stamp));

    snap_req_t gate = want_photo ? photo_gate(false) : SNAP_REQ_SENT;
    photo_state_t initial = PH_NONE;
    if (want_photo) {
        initial = gate == SNAP_REQ_SENT ? PH_PENDING : (gate == SNAP_REQ_OFFLINE ? PH_FAIL : PH_SKIP);
    }
    uint32_t seq = events_add(stamp, type, info, initial);
    ESP_LOGI(TAG, "Event #%u %s (%s) photo=%d", (unsigned)seq, type, info ? info : "", (int)initial);

    if (want_photo && gate == SNAP_REQ_SENT) {
        start_request(seq, type);
    }
    return seq;
}

snap_req_t trigger_manual_photo(void)
{
    snap_req_t gate = photo_gate(true);
    char stamp[20];
    clock_stamp(stamp, sizeof(stamp));
    if (gate != SNAP_REQ_SENT) {
        /* Подію не створюємо, меню покаже причину відмови */
        return gate;
    }
    uint32_t seq = events_add(stamp, "manual", "menu", PH_PENDING);
    start_request(seq, "manual");
    return SNAP_REQ_SENT;
}

void triggers_poll(void)
{
    link_snap_done_t done;
    if (link_esp_take_snap_done(&done)) {
        if (pending_seq != 0) {
            uint32_t seq = pending_seq;
            pending_seq = 0;
            struct tm t;
            char hhmm[16] = "--:--";
            if (clock_get(&t)) {
                snprintf(hhmm, sizeof(hhmm), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
            }
            snprintf(status.last_snap_reason, sizeof(status.last_snap_reason), "%s", pending_reason);
            snprintf(status.last_snap_time, sizeof(status.last_snap_time), "%s", hhmm);
            status.last_snap_ok = done.ok ? 1 : 0;
            if (done.ok) {
                snprintf(status.last_snap_file, sizeof(status.last_snap_file), "%s", done.file);
                events_set_photo(seq, PH_OK, done.file);
            } else {
                events_set_photo(seq, PH_FAIL, done.err[0] ? done.err : "error");
            }
            ESP_LOGI(TAG, "snap_done ok=%d file=%s err=%s", done.ok, done.file, done.err);
        } else {
            ESP_LOGW(TAG, "snap_done without a pending request");
        }
    }
    if (pending_seq != 0 && esp_timer_get_time() > pending_deadline_us) {
        ESP_LOGW(TAG, "snap_done timeout for event %u", (unsigned)pending_seq);
        events_set_photo(pending_seq, PH_FAIL, "timeout");
        status.last_snap_ok = 0;
        pending_seq = 0;
    }
}
