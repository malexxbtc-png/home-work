#include "actuator_client.h"
#include "tg_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "ACTUATOR_CLIENT";

typedef struct {
    char *buf;
    size_t max_len;
    size_t current_len;
} http_buffer_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        http_buffer_t *buf = (http_buffer_t *)evt->user_data;
        if (buf && buf->buf) {
            size_t copy_len = evt->data_len;
            if (buf->current_len + copy_len >= buf->max_len) {
                copy_len = buf->max_len - buf->current_len - 1;
            }
            if (copy_len > 0) {
                memcpy(buf->buf + buf->current_len, evt->data, copy_len);
                buf->current_len += copy_len;
                buf->buf[buf->current_len] = '\0';
            }
        }
    }
    return ESP_OK;
}

esp_err_t actuator_get_status(char* out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return ESP_ERR_INVALID_ARG;
    out_buf[0] = '\0';

    char url[128];
    snprintf(url, sizeof(url), "http://%s/status", ACTUATOR_IP);

    http_buffer_t http_buf = {
        .buf = out_buf,
        .max_len = max_len,
        .current_len = 0
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &http_buf,
        .timeout_ms = 4000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET /status failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t send_simple_get(const char* path) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s%s", ACTUATOR_IP, path);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 4000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s", path, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t actuator_set_led(int id, int level) {
    char path[64];
    snprintf(path, sizeof(path), "/set_led?id=%d&level=%d", id, level);
    return send_simple_get(path);
}

esp_err_t actuator_set_angle(int deg) {
    char path[64];
    snprintf(path, sizeof(path), "/set_angle?deg=%d", deg);
    return send_simple_get(path);
}

esp_err_t actuator_play_sound(int melody_id) {
    char path[64];
    snprintf(path, sizeof(path), "/play_sound?id=%d", melody_id);
    return send_simple_get(path);
}