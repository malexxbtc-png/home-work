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
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "camera.h"
#include "display.h"
#include "forecast.h"
#include "link_pico.h"
#include "sd_card.h"

static const char *TAG = "app_main";

#define HEARTBEAT_INTERVAL_MS    5000
#define WEATHER_SEND_INTERVAL_MS 60000
#define TIME_SEND_INTERVAL_MS    60000
#define VALID_EPOCH_MIN          1704067200LL
#define NO_WEATHER_AGE           999          /* Pico сприймає >=999 як «немає даних» */

/* ---------- допоміжне ---------- */

/* Кількість діб від 1970-01-01 до цивільної дати (алгоритм Говарда Хіннанта) */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
  y -= (m <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

/* Unix-час «настінного» годинника: UTC + зсув часової зони (з урахуванням літнього часу).
   Pico розбирає це число як UTC і отримує локальний час Києва. */
static int64_t local_wall_epoch(time_t utc)
{
  struct tm lt;
  localtime_r(&utc, &lt);
  return days_from_civil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday) * 86400 +
         lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

static double r1(double value)
{
  return round(value * 10.0) / 10.0;
}

/* ---------- повідомлення на Pico ---------- */

static void send_time_if_synchronized(void)
{
  time_t utc = time(NULL);
  if (utc < VALID_EPOCH_MIN) {
    return;
  }
  char json[64];
  int length = snprintf(json, sizeof(json), "{\"t\":\"time\",\"epoch\":%lld}",
                        (long long)local_wall_epoch(utc));
  if (length > 0 && (size_t)length < sizeof(json)) {
    link_pico_send_json(json);
  }
}

static void send_heartbeat(void)
{
  wifi_ap_record_t ap_info;
  int rssi = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK ? ap_info.rssi : -127;

  /* Pico показує вік погоди в хвилинах */
  int wx_age_min = NO_WEATHER_AGE;
  current_wx_t weather;
  if (forecast_get_current(&weather) && weather.fetched_at_us > 0) {
    int64_t age_s = (esp_timer_get_time() - weather.fetched_at_us) / 1000000;
    if (age_s >= 0 && age_s / 60 < NO_WEATHER_AGE) {
      wx_age_min = (int)(age_s / 60);
    }
  }

  char json[128];
  int length = snprintf(json, sizeof(json),
                        "{\"t\":\"hb\",\"wifi\":%d,\"cam\":%d,\"sd\":%d,\"tg\":0,\"wx_age\":%d}",
                        rssi, camera_is_ready() ? 1 : 0, sd_card_is_ready() ? 1 : 0, wx_age_min);
  if (length > 0 && (size_t)length < sizeof(json)) {
    link_pico_send_json(json);
  }
}

/* Формат, який очікує Pico:
   {"t":"wx","cur":{"w","g","dir","rain","snow","RH","cloud"},
    "fc":[{"cond","tmin","tmax","pop","wmax"}, ...]} */
static void send_weather(void)
{
  current_wx_t weather;
  if (!forecast_get_current(&weather) || weather.is_stale) {
    return;   /* застарілі дані не надсилаємо: вік на Pico зростає, і тривоги за погодою вимикаються */
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *current = cJSON_CreateObject();
  cJSON *days = cJSON_CreateArray();
  if (root == NULL || current == NULL || days == NULL) {
    ESP_LOGE(TAG, "Failed to allocate weather JSON");
    cJSON_Delete(root);
    cJSON_Delete(current);
    cJSON_Delete(days);
    return;
  }

  cJSON_AddStringToObject(root, "t", "wx");
  cJSON_AddNumberToObject(current, "w", r1(weather.wind_speed));
  cJSON_AddNumberToObject(current, "g", r1(weather.wind_gusts));
  cJSON_AddNumberToObject(current, "dir", round(weather.wind_direction));
  cJSON_AddNumberToObject(current, "rain", r1(weather.precipitation));
  cJSON_AddNumberToObject(current, "snow", r1(weather.snowfall));
  if (weather.humidity_valid) {
    cJSON_AddNumberToObject(current, "RH", round(weather.humidity));
  } else {
    cJSON_AddNullToObject(current, "RH");
  }
  cJSON_AddNumberToObject(current, "cloud", weather.cloud_cover);
  cJSON_AddItemToObject(root, "cur", current);

  for (uint8_t i = 0; i < weather.forecast_days; i++) {
    cJSON *day = cJSON_CreateObject();
    if (day == NULL) {
      ESP_LOGE(TAG, "Failed to allocate forecast-day JSON");
      cJSON_Delete(root);
      cJSON_Delete(days);
      return;
    }
    cJSON_AddNumberToObject(day, "cond", weather.daily[i].weather_code);
    cJSON_AddNumberToObject(day, "tmin", r1(weather.daily[i].temperature_min));
    cJSON_AddNumberToObject(day, "tmax", r1(weather.daily[i].temperature_max));
    cJSON_AddNumberToObject(day, "pop", round(weather.daily[i].precipitation_probability_max));
    cJSON_AddNumberToObject(day, "wmax", r1(weather.daily[i].wind_speed_max));
    cJSON_AddItemToArray(days, day);
  }
  cJSON_AddItemToObject(root, "fc", days);

  char *json = cJSON_PrintUnformatted(root);
  if (json == NULL) {
    ESP_LOGE(TAG, "Failed to serialize weather JSON");
  } else {
    link_pico_send_json(json);
    free(json);
  }
  cJSON_Delete(root);
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
  TickType_t last_weather = 0;
  TickType_t last_time = 0;
  int64_t last_sent_fetch_us = 0;
  bool first = true;

  while (1) {
    TickType_t now = xTaskGetTickCount();

    if (first || (now - last_heartbeat) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)) {
      send_heartbeat();
      last_heartbeat = now;
    }

    /* Погоду надсилаємо раз на хвилину і одразу після кожного нового запиту до Open-Meteo */
    current_wx_t weather;
    bool fresh = forecast_get_current(&weather) && weather.fetched_at_us != last_sent_fetch_us;
    if (first || fresh || (now - last_weather) >= pdMS_TO_TICKS(WEATHER_SEND_INTERVAL_MS)) {
      send_weather();
      last_weather = now;
      if (fresh) last_sent_fetch_us = weather.fetched_at_us;
    }

    if (first || (now - last_time) >= pdMS_TO_TICKS(TIME_SEND_INTERVAL_MS)) {
      send_time_if_synchronized();
      last_time = now;
    }

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

  forecast_init();          /* також задає часову зону */
  link_pico_init();
  display_init();           /* дисплей на шині SPI2 */
  sd_card_init();           /* SD на власній шині SPI3 */
#if CONFIG_WEATHER_CAMERA_ENABLED && CONFIG_WEATHER_CAMERA_INIT_AT_BOOT
  camera_init();            /* збій камери не зупиняє решту; повторна спроба буде на запиті знімка */
#endif

  forecast_start_task();
  display_start_task();

  snap_queue = xQueueCreate(2, sizeof(snap_request_t));
  if (snap_queue == NULL ||
      xTaskCreate(snap_task, "snap_task", 8192, NULL, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create the snapshot task");
  }
  if (xTaskCreate(communication_task, "communication_task", 6144, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Pico communication task");
  }
  ESP_LOGI(TAG, "Components initialized");
}
