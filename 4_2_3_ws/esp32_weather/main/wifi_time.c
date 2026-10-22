#include "wifi_time.h"

#include <string.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define CONNECTED_BIT BIT0
#define RETRY_DELAY_US (3 * 1000 * 1000)

static const char *TAG = "wifi";

static EventGroupHandle_t s_eg;
static esp_timer_handle_t s_retry_timer;
static volatile wifi_state_t s_state = WIFI_ST_OFF;
static volatile bool s_running;
static volatile bool s_ntp_synced;
static bool s_sntp_started;

static const char *reason_text(int r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:          return "NO_AP_FOUND (wrong name, 5 GHz only, too far)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "AUTH_FAIL/HANDSHAKE_TIMEOUT (wrong password or WPA3-only)";
    case WIFI_REASON_ASSOC_FAIL:           return "ASSOC_FAIL (MAC filter, client limit)";
    case WIFI_REASON_BEACON_TIMEOUT:       return "BEACON_TIMEOUT (weak signal)";
    case WIFI_REASON_ASSOC_LEAVE:          return "ASSOC_LEAVE (stopped by us)";
    default:                               return "other";
    }
}

static void retry_cb(void *arg)
{
    if (s_running) {
        esp_wifi_connect();
    }
}

static void ntp_sync_cb(struct timeval *tv)
{
    s_ntp_synced = true;     /* handled in the main loop, not in the lwIP context */
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = WIFI_ST_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        xEventGroupClearBits(s_eg, CONNECTED_BIT);
        if (s_running) {
            s_state = WIFI_ST_CONNECTING;
            ESP_LOGW(TAG, "disconnected, reason=%d %s", e->reason, reason_text(e->reason));
            esp_timer_start_once(s_retry_timer, RETRY_DELAY_US);   /* simple back-off */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_state = WIFI_ST_CONNECTED;
        xEventGroupSetBits(s_eg, CONNECTED_BIT);

        if (!s_sntp_started) {
            esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            cfg.sync_cb = ntp_sync_cb;
            if (esp_netif_sntp_init(&cfg) == ESP_OK) {
                s_sntp_started = true;   /* re-syncs every CONFIG_LWIP_SNTP_UPDATE_DELAY (1 h) */
            }
        }
    }
}

esp_err_t wifi_time_start(void)
{
#if !CONFIG_WS_WIFI_ENABLE
    s_state = WIFI_ST_OFF;
    return ESP_OK;
#else
    const char *ssid = CONFIG_WS_WIFI_SSID;
    if (ssid[0] == '\0' || strcmp(ssid, "YOUR_WIFI_SSID") == 0) {
        ESP_LOGW(TAG, "SSID is not set (menuconfig -> Weather station)");
        s_state = WIFI_ST_NO_SSID;
        return ESP_OK;
    }

    s_eg = xEventGroupCreate();
    const esp_timer_create_args_t targs = {.callback = retry_cb, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, CONFIG_WS_WIFI_PASSWORD, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_running = true;
    s_state = WIFI_ST_CONNECTING;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    return esp_wifi_start();
#endif
}

void wifi_time_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    esp_timer_stop(s_retry_timer);
    esp_wifi_stop();
}

wifi_state_t wifi_time_state(void)
{
    return s_state;
}

bool wifi_time_wait_connected(uint32_t timeout_ms)
{
    if (!s_eg) {
        return false;
    }
    return xEventGroupWaitBits(s_eg, CONNECTED_BIT, pdFALSE, pdTRUE,
                               pdMS_TO_TICKS(timeout_ms)) & CONNECTED_BIT;
}

bool wifi_time_take_ntp_synced(void)
{
    if (s_ntp_synced) {
        s_ntp_synced = false;
        return true;
    }
    return false;
}
