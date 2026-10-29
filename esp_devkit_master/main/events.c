#include "events.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "events";

#define NVS_NS  "events"
#define NVS_KEY "last"

static event_t ring[EVENT_LOG_SIZE];
static int head;            /* індекс наступного запису */
static int count;
static uint32_t next_seq = 1;
static SemaphoreHandle_t lock;

static void persist(const event_t *ev)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(h, NVS_KEY, ev, sizeof(*ev)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

static void copy_str(char *dst, size_t size, const char *src)
{
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

void events_init(void)
{
    if (lock == NULL) {
        lock = xSemaphoreCreateMutex();
    }
    head = count = 0;
    next_seq = 1;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    event_t stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(h, NVS_KEY, &stored, &size) == ESP_OK && size == sizeof(stored)) {
        stored.stamp[sizeof(stored.stamp) - 1] = '\0';
        stored.type[sizeof(stored.type) - 1] = '\0';
        stored.info[sizeof(stored.info) - 1] = '\0';
        stored.file[sizeof(stored.file) - 1] = '\0';
        if (stored.photo == PH_PENDING) {
            stored.photo = PH_FAIL;                 /* перезавантаження перервало очікування */
        }
        stored.seq = next_seq++;
        ring[head] = stored;
        head = (head + 1) % EVENT_LOG_SIZE;
        count = 1;
        ESP_LOGI(TAG, "Restored last event: %s at %s", stored.type, stored.stamp);
    }
    nvs_close(h);
}

uint32_t events_add(const char *stamp, const char *type, const char *info, photo_state_t photo)
{
    if (lock == NULL) {
        return 0;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    event_t *ev = &ring[head];
    memset(ev, 0, sizeof(*ev));
    ev->seq = next_seq++;
    copy_str(ev->stamp, sizeof(ev->stamp), stamp);
    copy_str(ev->type, sizeof(ev->type), type);
    copy_str(ev->info, sizeof(ev->info), info);
    ev->photo = photo;
    head = (head + 1) % EVENT_LOG_SIZE;
    if (count < EVENT_LOG_SIZE) {
        count++;
    }
    uint32_t seq = ev->seq;
    event_t copy = *ev;
    xSemaphoreGive(lock);
    persist(&copy);
    return seq;
}

void events_set_photo(uint32_t seq, photo_state_t state, const char *file)
{
    if (lock == NULL) {
        return;
    }
    event_t copy;
    bool found = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    for (int i = 0; i < count; i++) {
        event_t *ev = &ring[(head - 1 - i + 2 * EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
        if (ev->seq == seq) {
            ev->photo = state;
            if (file != NULL) {
                copy_str(ev->file, sizeof(ev->file), file);
            }
            copy = *ev;
            found = true;
            break;
        }
    }
    bool is_last = found && copy.seq == next_seq - 1;
    xSemaphoreGive(lock);
    if (is_last) {
        persist(&copy);
    }
}

int events_count(void)
{
    return count;
}

bool events_get(int back, event_t *out)
{
    if (lock == NULL || out == NULL || back < 0) {
        return false;
    }
    bool ok = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (back < count) {
        *out = ring[(head - 1 - back + 2 * EVENT_LOG_SIZE) % EVENT_LOG_SIZE];
        ok = true;
    }
    xSemaphoreGive(lock);
    return ok;
}
