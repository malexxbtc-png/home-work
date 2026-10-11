#include <stdio.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "buzzer.h"
#include "leds.h"
#include "servo.h"
#include "web_server.h"

static const char *TAG = "WEB_SERVER";

// GET /status -> Повертає статус 3 світлодіодів та кута серво у форматі JSON
static esp_err_t status_handler(httpd_req_t *req) {
    char resp[128];
    int l0 = led_get_level(0);
    int l1 = led_get_level(1);
    int l2 = led_get_level(2);
    int angle = servo_get_angle();

    snprintf(resp, sizeof(resp), 
        "{\"leds\":[%d,%d,%d],\"angle\":%d}", 
        l0, l1, l2, angle);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /set_led?id=1&level=4
static esp_err_t set_led_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        int id = 0, level = 0;
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            id = atoi(param);
        }
        if (httpd_query_key_value(query, "level", param, sizeof(param)) == ESP_OK) {
            level = atoi(param);
        }
        // id від 1 до 3 (масиву передаємо id - 1: від 0 до 2)
        if (id >= 1 && id <= 3) {
            led_set_level(id - 1, level);
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /set_angle?deg=90
static esp_err_t set_angle_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        int deg = 0;
        if (httpd_query_key_value(query, "deg", param, sizeof(param)) == ESP_OK) {
            deg = atoi(param);
        }
        servo_set_angle(deg);
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /play_sound?id=1
static esp_err_t play_sound_handler(httpd_req_t *req) {
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        int id = 1;
        if (httpd_query_key_value(query, "id", param, sizeof(param)) == ESP_OK) {
            id = atoi(param);
        }
        if (id == 0) {
            buzzer_play_melody_async(0); // Зупинити / тиша
        } else if (id >= 1 && id <= PLAYLIST_SIZE) {
            buzzer_play_melody_async(id);
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /master_switch?level=0 (або 4)
static esp_err_t master_switch_handler(httpd_req_t *req) {
    char query[64];
    int level = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(query, "level", param, sizeof(param)) == ESP_OK) {
            level = atoi(param);
        }
    }
    for (int i = 0; i < 3; i++) {
        led_set_level(i, level);
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_web_server(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &status_uri);

        httpd_uri_t set_led_uri = { .uri = "/set_led", .method = HTTP_GET, .handler = set_led_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &set_led_uri);

        httpd_uri_t set_angle_uri = { .uri = "/set_angle", .method = HTTP_GET, .handler = set_angle_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &set_angle_uri);

        httpd_uri_t play_sound_uri = { .uri = "/play_sound", .method = HTTP_GET, .handler = play_sound_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &play_sound_uri);

        httpd_uri_t master_switch_uri = { .uri = "/master_switch", .method = HTTP_GET, .handler = master_switch_handler }; 
        httpd_register_uri_handler(server, &master_switch_uri);

        ESP_LOGI(TAG, "Web server started successfully with 3 LEDs & Melodies support");
    }
}