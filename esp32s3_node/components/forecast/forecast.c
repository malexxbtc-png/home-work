#include "forecast.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

static const char *TAG = "forecast";

#define VALID_EPOCH_MIN 1704067200LL

static current_wx_t cached_wx;
static bool cached_valid;
static SemaphoreHandle_t wx_mutex;

static float number(const cJSON *object, const char *key, float fallback)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsNumber(item) && isfinite(item->valuedouble) ? (float)item->valuedouble : fallback;
}

static bool has_number(const cJSON *object, const char *key)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsNumber(item) && isfinite(item->valuedouble);
}

void forecast_init(void)
{
  /* Майстер надсилає місцевий «настінний» час, тому вузол працює в UTC без зсувів */
  setenv("TZ", "UTC0", 1);
  tzset();
  wx_mutex = xSemaphoreCreateMutex();
  if (wx_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create weather cache mutex");
  }
}

bool forecast_update_from_json(const char *json)
{
  if (json == NULL || wx_mutex == NULL) {
    return false;
  }
  cJSON *root = cJSON_Parse(json);
  if (root == NULL) {
    return false;
  }
  const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cur");
  if (!cJSON_IsObject(cur) || !has_number(cur, "T")) {
    ESP_LOGW(TAG, "wx message without cur.T ignored");
    cJSON_Delete(root);
    return false;
  }

  static current_wx_t parsed;               /* поза стеком */
  memset(&parsed, 0, sizeof(parsed));
  parsed.temp = number(cur, "T", 0);
  parsed.humidity_valid = has_number(cur, "RH");
  parsed.humidity = parsed.humidity_valid ? number(cur, "RH", 0) : NAN;
  parsed.precipitation = number(cur, "rain", 0);
  parsed.snowfall = number(cur, "snow", 0);
  parsed.weather_code = (int)number(cur, "code", 0);
  parsed.cloud_cover = (int)number(cur, "cloud", 0);
  parsed.wind_speed = number(cur, "w", 0);
  parsed.wind_gusts = number(cur, "g", 0);
  parsed.wind_direction = number(cur, "dir", 0);

  const cJSON *fc = cJSON_GetObjectItemCaseSensitive(root, "fc");
  const cJSON *day;
  cJSON_ArrayForEach(day, fc) {
    if (parsed.forecast_days >= FORECAST_MAX_DAYS) {
      break;
    }
    forecast_day_t *d = &parsed.daily[parsed.forecast_days++];
    d->weather_code = (int)number(day, "cond", 0);
    d->temperature_min = number(day, "tmin", NAN);
    d->temperature_max = number(day, "tmax", NAN);
    d->precipitation_probability_max = number(day, "pop", 0);
    d->wind_speed_max = number(day, "wmax", 0);
  }

  /* Вік даних рахує майстер: повторна відправка тих самих даних не робить їх «свіжими» */
  float age_s = number(root, "age", 0);
  if (age_s < 0) age_s = 0;
  parsed.fetched_at_us = esp_timer_get_time() - (int64_t)(age_s * 1000000.0f);
  cJSON_Delete(root);

  if (xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return false;
  }
  cached_wx = parsed;
  cached_valid = true;
  xSemaphoreGive(wx_mutex);
  return true;
}

void forecast_set_local_time(int64_t local_epoch)
{
  if (local_epoch < VALID_EPOCH_MIN) {
    return;
  }
  time_t now = time(NULL);
  if (now >= (time_t)VALID_EPOCH_MIN && llabs((long long)(now - local_epoch)) <= 1) {
    return;                                 /* розбіжність у 1 с не чіпаємо */
  }
  struct timeval tv = {.tv_sec = (time_t)local_epoch, .tv_usec = 0};
  if (settimeofday(&tv, NULL) == 0) {
    ESP_LOGI(TAG, "Clock set from master (local time %lld)", (long long)local_epoch);
  }
}

bool forecast_get_current(current_wx_t *weather)
{
  if (weather == NULL || wx_mutex == NULL ||
      xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return false;
  }
  *weather = cached_wx;
  bool available = cached_valid;
  if (available) {
    int64_t age_us = esp_timer_get_time() - cached_wx.fetched_at_us;
    weather->is_stale = age_us < 0 ||
        age_us > (int64_t)CONFIG_WEATHER_FORECAST_STALE_AFTER_SECONDS * 1000000;
  }
  xSemaphoreGive(wx_mutex);
  return available;
}
