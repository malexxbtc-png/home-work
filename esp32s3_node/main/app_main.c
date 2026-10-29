#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "camera.h"
#include "display.h"
#include "forecast.h"
#include "link_pico.h"
#include "sd_card.h"

static const char *TAG = "app_main";

#define HEARTBEAT_INTERVAL_MS    5000

/* ---------- повідомлення майстру ---------- */

/* hb: стан камери і SD. Wi-Fi і погода тепер на майстрі. */
static void send_heartbeat(void)
{
  char json[96];
  int length = snprintf(json, sizeof(json), "{\"t\":\"hb\",\"cam\":%d,\"sd\":%d,\"tg\":0}",
                        camera_is_ready() ? 1 : 0, sd_card_is_ready() ? 1 : 0);
  if (length > 0 && (size_t)length < sizeof(json)) {
    link_pico_send_json(json);
  }
}

/* Погода і час, які надсилає майстер: {"t":"wx",...} і {"t":"time","epoch":N} */
static void handle_master_wx_and_time(void)
{
  char message[CONFIG_WEATHER_LINK_PICO_MAX_JSON_SIZE];
  if (link_pico_take_wx(message, sizeof(message))) {
    forecast_update_from_json(message);
  }
  int64_t epoch;
  if (link_pico_take_time(&epoch)) {
    forecast_set_local_time(epoch);
  }
}

/* ---------- знімки за запитом майстра ---------- */

typedef struct {
  char reason[16];
  unsigned seq;
} snap_request_t;

static QueueHandle_t snap_queue;

static void send_snap_done(bool ok, const char *file, const char *err, unsigned seq)
{
  char json[160];
  int length = snprintf(json, sizeof(json),
                        "{\"t\":\"snap_done\",\"ok\":%s,\"file\":\"%s\",\"err\":\"%s\",\"seq\":%u}",
                        ok ? "true" : "false", file, err, seq);
  if (length > 0 && (size_t)length < sizeof(json)) {
    link_pico_send_json(json);
  }
}

/* Приймає запит {"t":"snap","reason":"pir","seq":N} і ставить його в чергу для snap_task */
static void handle_master_snap(void)
{
  char request[CONFIG_WEATHER_LINK_PICO_MAX_JSON_SIZE];
  if (!link_pico_get_latest_snap(request, sizeof(request))) {
    return;
  }
  snap_request_t item = {.reason = "unknown", .seq = 0};
  cJSON *snap = cJSON_Parse(request);
  if (snap != NULL) {
    const cJSON *reason = cJSON_GetObjectItemCaseSensitive(snap, "reason");
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(snap, "seq");
    if (cJSON_IsString(reason) && reason->valuestring != NULL) {
      snprintf(item.reason, sizeof(item.reason), "%s", reason->valuestring);
    }
    if (cJSON_IsNumber(seq)) {
      item.seq = (unsigned)seq->valuedouble;
    }
    cJSON_Delete(snap);
  }
  if (snap_queue == NULL || xQueueSend(snap_queue, &item, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Snapshot '%s' rejected: queue is full", item.reason);
    send_snap_done(false, "", "busy", item.seq);
  }
}

/* Знімає кадр і пише його на SD. Окрема задача, щоб 1-2 с кодування не блокували heartbeat. */
static void snap_task(void *arg)
{
  (void)arg;
  snap_request_t item;
  while (1) {
    if (xQueueReceive(snap_queue, &item, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    camera_result_t result;
    camera_snap(item.reason, &result);

    float t_local = NAN, rh_local = NAN, p_local = NAN;
    char data[CONFIG_WEATHER_LINK_PICO_MAX_JSON_SIZE];
    if (link_pico_get_latest_data(data, sizeof(data), NULL)) {
      cJSON *root = cJSON_Parse(data);
      if (root != NULL) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "T");
        const cJSON *rh = cJSON_GetObjectItemCaseSensitive(root, "RH");
        const cJSON *p = cJSON_GetObjectItemCaseSensitive(root, "P");
        if (cJSON_IsNumber(t)) t_local = (float)t->valuedouble;
        if (cJSON_IsNumber(rh)) rh_local = (float)rh->valuedouble;
        if (cJSON_IsNumber(p)) p_local = (float)p->valuedouble;
        cJSON_Delete(root);
      }
    }
    float t_out = NAN, wind = NAN;
    current_wx_t weather;
    if (forecast_get_current(&weather)) {
      t_out = weather.temp;
      wind = weather.wind_speed;
    }
    sd_card_log_event(item.reason, t_local, rh_local, p_local, t_out, wind, result.ok ? result.file : "");

    ESP_LOGI(TAG, "Snapshot '%s': %s %s", item.reason, result.ok ? "saved" : "FAILED",
             result.ok ? result.file : result.err);
    send_snap_done(result.ok, result.ok ? result.file : "", result.ok ? "" : result.err, item.seq);
  }
}

static void communication_task(void *arg)
{
  (void)arg;
  TickType_t last_heartbeat = 0;
  bool first = true;

  while (1) {
    TickType_t now = xTaskGetTickCount();
    if (first || (now - last_heartbeat) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)) {
      send_heartbeat();
      last_heartbeat = now;
    }
    handle_master_wx_and_time();
    handle_master_snap();
    first = false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void app_main(void)
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }

  ESP_LOGI(TAG, "Starting Weather Station ESP32-S3 Node");

  forecast_init();          /* приймач погоди/часу від майстра, TZ = UTC */
  link_pico_init();
  display_init();           /* дисплей на шині SPI2 */
  sd_card_init();           /* SD на власній шині SPI3 */
#if CONFIG_WEATHER_CAMERA_ENABLED && CONFIG_WEATHER_CAMERA_INIT_AT_BOOT
  camera_init();            /* збій камери не зупиняє решту; повторна спроба буде на запиті знімка */
#endif

  display_start_task();

  snap_queue = xQueueCreate(2, sizeof(snap_request_t));
  if (snap_queue == NULL ||
      xTaskCreate(snap_task, "snap_task", 8192, NULL, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create the snapshot task");
  }
  if (xTaskCreate(communication_task, "communication_task", 6144, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create master communication task");
  }
  ESP_LOGI(TAG, "Components initialized");
}
