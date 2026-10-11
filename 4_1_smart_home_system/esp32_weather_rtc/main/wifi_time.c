#include <string.h>
#include <stdlib.h>
#include "wifi_time.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_time";

static volatile wifi_st_t s_status = WIFI_ST_OFF;
static esp_timer_handle_t s_retry_timer;
static int s_fail_count;

static const char *reason_text(int r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND: мережу з таким SSID не видно (помилка в назві? мережа 5 ГГц? роутер далеко?)";
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL: невірний пароль (або роутер лише WPA3/WEP)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE_TIMEOUT: найімовірніше невірний пароль";
    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC_FAIL: роутер відмовив (ліміт клієнтів / MAC-фільтр?)";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT: слабкий сигнал або роутер зник";
    case WIFI_REASON_AUTH_EXPIRE:
        return "AUTH_EXPIRE: роутер не відповів на автентифікацію (слабкий сигнал?)";
    case WIFI_REASON_CONNECTION_FAIL:
        return "CONNECTION_FAIL";
    default:
        return "інша причина";
    }
}

static void retry_cb(void *arg)
{
    esp_wifi_connect();
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting to \"%s\"...", CONFIG_WS_WIFI_SSID);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        s_status = WIFI_ST_CONNECTING;
        s_fail_count++;
        ESP_LOGW(TAG, "WiFi не підключено (спроба %d), reason=%d  %s", s_fail_count, e->reason,
                 reason_text(e->reason));
        /* пауза між спробами 2 с, щоб не засипати роутер і лог */
        esp_timer_start_once(s_retry_timer, 2000 * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_status = WIFI_ST_CONNECTED;
        s_fail_count = 0;
        ESP_LOGI(TAG, "got IP " IPSTR ", starting SNTP", IP2STR(&e->ip_info.ip));
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_setservername(1, "time.google.com");
            esp_sntp_init();
        }
    }
}

void wifi_time_start(void)
{
#if !CONFIG_WS_WIFI_ENABLE
    s_status = WIFI_ST_OFF;
    ESP_LOGI(TAG, "WiFi вимкнено в menuconfig, час тільки з RTC");
    return;
#else
    if (strcmp(CONFIG_WS_WIFI_SSID, "YOUR_WIFI_SSID") == 0 || CONFIG_WS_WIFI_SSID[0] == '\0') {
        s_status = WIFI_ST_NOT_CONFIGURED;
        ESP_LOGE(TAG, "WiFi SSID не задано! idf.py menuconfig -> Weather station -> WiFi SSID/password");
        return;
    }
    size_t plen = strlen(CONFIG_WS_WIFI_PASSWORD);
    if (plen > 0 && plen < 8) {
        ESP_LOGW(TAG, "Пароль WPA має бути 8..63 символи, зараз %u - підключення неможливе", (unsigned)plen);
    }
    s_status = WIFI_ST_CONNECTING;

    /* NVS потрібен для WiFi */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, CONFIG_WS_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, CONFIG_WS_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
    /* мінімальний захист: WPA (приймає WPA/WPA2/WPA3-перехідний); відкрита мережа - якщо пароль порожній */
    wc.sta.threshold.authmode = (plen == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    /* Україна: канали 1-13. За замовчуванням ESP32 дозволяє лише 1-11, і роутер на 12/13 не видно */
    err = esp_wifi_set_country_code("UA", true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_country_code: %s", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
#endif
}

wifi_st_t wifi_time_status(void)
{
    return s_status;
}

bool wifi_time_get_utc(time_t *out)
{
    time_t now = time(NULL);
    if (now < 1704067200) {   /* до 2024-01-01 = SNTP ще не синхронізував */
        return false;
    }
    *out = now;
    return true;
}
