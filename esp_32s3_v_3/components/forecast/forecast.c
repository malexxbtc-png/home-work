#include "forecast.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "forecast";

#define WIFI_CONNECTED_BIT BIT0
#define VALID_EPOCH_MIN    1704067200LL

typedef struct {
  char *body;
  size_t capacity;
  size_t length;
  bool overflow;
} http_response_t;

static current_wx_t cached_wx;
static SemaphoreHandle_t wx_mutex;
static EventGroupHandle_t wifi_events;
static bool wifi_initialized;
static bool sntp_initialized;

static bool get_number(const cJSON *object, const char *key, double *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
    return false;
  }
  *value = item->valuedouble;
  return true;
}

static bool get_array_number(const cJSON *object, const char *key, int index, double *value)
{
  const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
  const cJSON *item = cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
    return false;
  }
  *value = item->valuedouble;
  return true;
}

/* ---------- Wi-Fi ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  (void)arg;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
    ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *event = event_data;
    ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
  }
}

static esp_err_t wifi_init_sta(void)
{
  if (wifi_initialized) {
    return ESP_OK;
  }
  if (CONFIG_WEATHER_FORECAST_WIFI_SSID[0] == '\0') {
    ESP_LOGE(TAG, "Wi-Fi SSID is empty; set it in menuconfig (Weather forecast)");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize network interface");
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return err;
  }
  if (esp_netif_create_default_wifi_sta() == NULL) {
    return ESP_FAIL;
  }

  wifi_events = xEventGroupCreate();
  if (wifi_events == NULL) {
    return ESP_ERR_NO_MEM;
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "initialize Wi-Fi");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  wifi_event_handler, NULL),
                      TAG, "register Wi-Fi event handler");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  wifi_event_handler, NULL),
                      TAG, "register IP event handler");

  wifi_config_t wifi_config = {0};
  snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s",
           CONFIG_WEATHER_FORECAST_WIFI_SSID);
  snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s",
           CONFIG_WEATHER_FORECAST_WIFI_PASSWORD);
  wifi_config.sta.threshold.authmode =
      CONFIG_WEATHER_FORECAST_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "configure station");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
  wifi_initialized = true;
  return ESP_OK;
}

static bool wait_for_wifi(void)
{
  EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                         pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_WIFI_CONNECT_TIMEOUT_MS));
  return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void initialize_sntp(void)
{
  if (sntp_initialized) {
    return;
  }
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, CONFIG_WEATHER_FORECAST_SNTP_SERVER);
  esp_sntp_init();
  sntp_initialized = true;
}

/* ---------- Open-Meteo ---------- */

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
    return ESP_OK;
  }

  http_response_t *response = event->user_data;
  size_t incoming = (size_t)event->data_len;
  if (response == NULL || incoming > response->capacity - response->length - 1) {
    if (response != NULL) {
      response->overflow = true;
    }
    return ESP_FAIL;
  }

  memcpy(response->body + response->length, event->data, incoming);
  response->length += incoming;
  response->body[response->length] = '\0';
  return ESP_OK;
}

static bool parse_current(const cJSON *root, current_wx_t *weather)
{
  const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
  double number;

  if (!cJSON_IsObject(current) || !get_number(current, "temperature_2m", &number)) {
    return false;
  }
  weather->temp = (float)number;
  weather->humidity_valid = get_number(current, "relative_humidity_2m", &number);
  weather->humidity = weather->humidity_valid ? (float)number : NAN;

  if (!get_number(current, "apparent_temperature", &number)) return false;
  weather->app_temp = (float)number;
  if (!get_number(current, "precipitation", &number)) return false;
  weather->precipitation = (float)number;
  if (!get_number(current, "snowfall", &number)) return false;
  weather->snowfall = (float)number;
  if (!get_number(current, "weather_code", &number)) return false;
  weather->weather_code = (int)number;
  if (!get_number(current, "cloud_cover", &number)) return false;
  weather->cloud_cover = (int)number;
  if (!get_number(current, "pressure_msl", &number)) return false;
  weather->pressure = (float)number;
  if (!get_number(current, "wind_speed_10m", &number)) return false;
  weather->wind_speed = (float)number;
  if (!get_number(current, "wind_gusts_10m", &number)) return false;
  weather->wind_gusts = (float)number;
  if (!get_number(current, "wind_direction_10m", &number)) return false;
  weather->wind_direction = (float)number;
  return true;
}

static void parse_daily(const cJSON *root, current_wx_t *weather)
{
  const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
  const cJSON *dates = daily != NULL ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
  if (!cJSON_IsArray(dates)) {
    return;
  }

  int count = cJSON_GetArraySize(dates);
  if (count > FORECAST_MAX_DAYS) {
    count = FORECAST_MAX_DAYS;
  }
  for (int i = 0; i < count; i++) {
    const cJSON *date = cJSON_GetArrayItem(dates, i);
    forecast_day_t day = {0};
    double value;
    if (!cJSON_IsString(date) || date->valuestring == NULL || strlen(date->valuestring) != 10) {
      continue;
    }
    snprintf(day.date, sizeof(day.date), "%s", date->valuestring);
    if (!get_array_number(daily, "temperature_2m_min", i, &value)) continue;
    day.temperature_min = (float)value;
    if (!get_array_number(daily, "temperature_2m_max", i, &value)) continue;
    day.temperature_max = (float)value;
    if (!get_array_number(daily, "precipitation_probability_max", i, &value)) continue;
    day.precipitation_probability_max = (float)value;
    if (!get_array_number(daily, "wind_speed_10m_max", i, &value)) continue;
    day.wind_speed_max = (float)value;
    if (!get_array_number(daily, "weather_code", i, &value)) continue;
    day.weather_code = (int)value;
    weather->daily[weather->forecast_days++] = day;
  }
}

/* Замінює '/' на %2F у назві часової зони */
static void encode_timezone(const char *in, char *out, size_t out_size)
{
  size_t n = 0;
  for (; *in != '\0' && n + 4 < out_size; in++) {
    if (*in == '/') {
      out[n++] = '%'; out[n++] = '2'; out[n++] = 'F';
    } else {
      out[n++] = *in;
    }
  }
  out[n] = '\0';
}

static bool fetch_open_meteo(void)
{
  char timezone[64];
  encode_timezone(CONFIG_WEATHER_FORECAST_TIMEZONE, timezone, sizeof(timezone));

  int days = CONFIG_WEATHER_FORECAST_DAYS;
  if (days > FORECAST_MAX_DAYS) days = FORECAST_MAX_DAYS;

  char url[768];
  int url_length = snprintf(
      url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,snowfall,"
      "weather_code,cloud_cover,pressure_msl,wind_speed_10m,wind_gusts_10m,wind_direction_10m"
      "&daily=temperature_2m_min,temperature_2m_max,precipitation_probability_max,"
      "wind_speed_10m_max,weather_code"
      "&forecast_days=%d&wind_speed_unit=ms&timezone=%s",
      CONFIG_WEATHER_FORECAST_LATITUDE, CONFIG_WEATHER_FORECAST_LONGITUDE, days, timezone);
  if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
    ESP_LOGE(TAG, "Open-Meteo URL exceeds buffer capacity");
    return false;
  }

  http_response_t response = {
      .capacity = CONFIG_WEATHER_FORECAST_HTTP_RESPONSE_BUFFER_SIZE,
  };
  response.body = calloc(response.capacity, sizeof(char));
  if (response.body == NULL) {
    ESP_LOGE(TAG, "Cannot allocate HTTP response buffer");
    return false;
  }

  esp_http_client_config_t config = {
      .url = url,
      .event_handler = http_event_handler,
      .user_data = &response,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = CONFIG_WEATHER_FORECAST_HTTP_TIMEOUT_MS,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Cannot initialize HTTP client");
    free(response.body);
    return false;
  }

  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK || status_code != 200 || response.overflow) {
    ESP_LOGW(TAG, "Open-Meteo request failed: %s, HTTP %d%s",
             esp_err_to_name(err), status_code,
             response.overflow ? ", response too large" : "");
    free(response.body);
    return false;
  }

  cJSON *root = cJSON_Parse(response.body);
  free(response.body);
  current_wx_t parsed = {0};
  if (root == NULL || !parse_current(root, &parsed)) {
    ESP_LOGW(TAG, "Open-Meteo response is missing required current fields");
    cJSON_Delete(root);
    return false;
  }
  parse_daily(root, &parsed);
  cJSON_Delete(root);

  time_t now = time(NULL);
  parsed.timestamp = now >= VALID_EPOCH_MIN ? (int64_t)now : 0;
  parsed.fetched_at_us = esp_timer_get_time();
  parsed.is_stale = false;

  if (xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_UPDATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGW(TAG, "Could not lock weather cache; response discarded");
    return false;
  }
  cached_wx = parsed;
  xSemaphoreGive(wx_mutex);
  ESP_LOGI(TAG, "Weather updated: %.1f C, wind %.1f m/s, %u forecast days",
           parsed.temp, parsed.wind_speed, (unsigned)parsed.forecast_days);
  return true;
}

static void forecast_task(void *arg)
{
  (void)arg;
  esp_err_t err = wifi_init_sta();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    if (!wait_for_wifi()) {
      ESP_LOGW(TAG, "No Wi-Fi connection yet; waiting");
      continue;
    }
    initialize_sntp();
    if (fetch_open_meteo()) {
      vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_WEATHER_FORECAST_UPDATE_INTERVAL_MINUTES * 60U * 1000U));
    } else {
      vTaskDelay(pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_WIFI_RETRY_INTERVAL_MS));
    }
  }
}

void forecast_init(void)
{
  setenv("TZ", CONFIG_WEATHER_FORECAST_POSIX_TZ, 1);
  tzset();

  wx_mutex = xSemaphoreCreateMutex();
  if (wx_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create weather cache mutex");
  }
}

void forecast_start_task(void)
{
  if (wx_mutex == NULL) {
    ESP_LOGE(TAG, "Weather task not started because its mutex is unavailable");
    return;
  }
  if (xTaskCreate(forecast_task, "forecast_task", 12288, NULL, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create weather task");
  }
}

bool forecast_get_current(current_wx_t *weather)
{
  if (weather == NULL || wx_mutex == NULL ||
      xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(CONFIG_WEATHER_FORECAST_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return false;
  }

  *weather = cached_wx;
  bool available = cached_wx.fetched_at_us > 0;
  if (available) {
    int64_t age_us = esp_timer_get_time() - cached_wx.fetched_at_us;
    weather->is_stale = age_us < 0 ||
        age_us > (int64_t)CONFIG_WEATHER_FORECAST_STALE_AFTER_SECONDS * 1000000;
  }
  xSemaphoreGive(wx_mutex);
  return available;
}
