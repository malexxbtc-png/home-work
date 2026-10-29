#include "net_wx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "config.h"
#include "wx_json.h"
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

static const char *TAG = "net_wx";

#define WIFI_CONNECTED_BIT BIT0

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    bool overflow;
} http_response_t;

static SemaphoreHandle_t wx_mutex;
static EventGroupHandle_t wifi_events;
static net_wx_t cached;
static int64_t cached_at_us;            /* 0 = даних немає */
static volatile bool sntp_started;
static volatile bool wifi_up;

/* ---------- час ---------- */

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool net_wx_local_epoch(int64_t *epoch_local)
{
    time_t utc = time(NULL);
    if (epoch_local == NULL || utc < (time_t)VALID_EPOCH_MIN) {
        return false;
    }
    struct tm lt;
    localtime_r(&utc, &lt);
    *epoch_local = days_from_civil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday) * 86400 +
                   lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return true;
}

/* ---------- Wi-Fi ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_up = false;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_up = true;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
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
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
                        TAG, "ip handler");

    wifi_config_t cfg = {0};
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", CONFIG_WEATHER_NET_WIFI_SSID);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", CONFIG_WEATHER_NET_WIFI_PASSWORD);
    cfg.sta.threshold.authmode =
        CONFIG_WEATHER_NET_WIFI_PASSWORD[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    return ESP_OK;
}

static bool wait_for_wifi(void)
{
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(CONFIG_WEATHER_NET_WIFI_CONNECT_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ---------- Open-Meteo ---------- */

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    http_response_t *r = event->user_data;
    size_t incoming = (size_t)event->data_len;
    if (r == NULL || incoming > r->capacity - r->length - 1) {
        if (r != NULL) {
            r->overflow = true;
        }
        return ESP_FAIL;
    }
    memcpy(r->body + r->length, event->data, incoming);
    r->length += incoming;
    r->body[r->length] = '\0';
    return ESP_OK;
}

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
    encode_timezone(CONFIG_WEATHER_NET_TIMEZONE, timezone, sizeof(timezone));

    char url[640];
    int n = snprintf(
        url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,relative_humidity_2m,precipitation,snowfall,"
        "weather_code,cloud_cover,wind_speed_10m,wind_gusts_10m,wind_direction_10m"
        "&daily=temperature_2m_min,temperature_2m_max,precipitation_probability_max,"
        "wind_speed_10m_max,weather_code"
        "&forecast_days=%d&wind_speed_unit=ms&timezone=%s",
        CONFIG_WEATHER_NET_LATITUDE, CONFIG_WEATHER_NET_LONGITUDE,
        CONFIG_WEATHER_NET_FORECAST_DAYS, timezone);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        ESP_LOGE(TAG, "Open-Meteo URL does not fit the buffer");
        return false;
    }

    http_response_t response = {.capacity = CONFIG_WEATHER_NET_HTTP_BUFFER_SIZE};
    response.body = calloc(response.capacity, 1);
    if (response.body == NULL) {
        ESP_LOGE(TAG, "No memory for the HTTP buffer");
        return false;
    }
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = CONFIG_WEATHER_NET_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(response.body);
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status_code != 200 || response.overflow) {
        ESP_LOGW(TAG, "Open-Meteo request failed: %s, HTTP %d%s", esp_err_to_name(err), status_code,
                 response.overflow ? ", response too large" : "");
        free(response.body);
        return false;
    }

    static net_wx_t parsed;                 /* поза стеком: структура ~0,3 КБ */
    bool ok = wx_json_parse_open_meteo(response.body, &parsed);
    free(response.body);
    if (!ok) {
        ESP_LOGW(TAG, "Open-Meteo response lacks required fields");
        return false;
    }
    if (xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    cached = parsed;
    cached_at_us = esp_timer_get_time();
    xSemaphoreGive(wx_mutex);
    ESP_LOGI(TAG, "Weather updated: %.1f C, wind %.1f m/s, %d forecast days",
             parsed.temp, parsed.wind, parsed.n_days);
    return true;
}

static void net_task(void *arg)
{
    (void)arg;
    esp_err_t err = wifi_init_sta();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        if (!wait_for_wifi()) {
            ESP_LOGW(TAG, "No Wi-Fi yet; waiting");
            continue;
        }
        if (!sntp_started) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, CONFIG_WEATHER_NET_SNTP_SERVER);
            esp_sntp_init();
            sntp_started = true;
        }
        uint32_t wait_ms = fetch_open_meteo()
                               ? (uint32_t)CONFIG_WEATHER_NET_UPDATE_INTERVAL_MINUTES * 60U * 1000U
                               : (uint32_t)CONFIG_WEATHER_NET_RETRY_INTERVAL_MS;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

/* ---------- API ---------- */

void net_wx_init(void)
{
    setenv("TZ", CONFIG_WEATHER_NET_POSIX_TZ, 1);
    tzset();
    wx_mutex = xSemaphoreCreateMutex();
}

void net_wx_start(void)
{
    if (CONFIG_WEATHER_NET_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty (menuconfig: Weather master network): outdoor weather disabled");
        return;
    }
    if (wx_mutex == NULL ||
        xTaskCreate(net_task, "net_wx", 10240, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start the network task");
    }
}

static int age_seconds_locked(void)
{
    return cached_at_us > 0 ? (int)((esp_timer_get_time() - cached_at_us) / 1000000) : -1;
}

bool net_wx_get_json(char *dest, size_t size)
{
    if (wx_mutex == NULL || dest == NULL || xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    int age = age_seconds_locked();
    bool ok = age >= 0 && age <= WX_MAX_AGE_MIN * 60 && wx_json_build(&cached, age, dest, size) > 0;
    xSemaphoreGive(wx_mutex);
    return ok;
}

int net_wx_age_min(void)
{
    int minutes = 999;
    if (wx_mutex != NULL && xSemaphoreTake(wx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int age = age_seconds_locked();
        if (age >= 0 && age / 60 < 999) {
            minutes = age / 60;
        }
        xSemaphoreGive(wx_mutex);
    }
    return minutes;
}

int net_wx_rssi(void)
{
    wifi_ap_record_t ap;
    return wifi_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
}
