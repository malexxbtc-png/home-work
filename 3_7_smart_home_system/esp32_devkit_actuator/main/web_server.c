#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "leds.h"
#include "servo.h"
#include "buzzer.h"
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "WEB_SERVER";

static esp_err_t status_handler(httpd_req_t *req) {
    char json_response[128];
    snprintf(json_response, sizeof(json_response),
             "{\"leds\":[%d,%d,%d,%d,%d,%d],\"angle\":%d}",
             led_get_level(0), led_get_level(1), led_get_level(2),
             led_get_level(3), led_get_level(4), led_get_level(5),
             servo_get_angle());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_led_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param_id[10], param_lvl[10];
        if (httpd_query_key_value(buf, "id", param_id, sizeof(param_id)) == ESP_OK &&
            httpd_query_key_value(buf, "level", param_lvl, sizeof(param_lvl)) == ESP_OK) {
            int id = atoi(param_id) - 1;
            int level = atoi(param_lvl);
            led_set_level(id, level);
        }
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_angle_handler(httpd_req_t *req) {
    char buf[100];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param_deg[10];
        if (httpd_query_key_value(buf, "deg", param_deg, sizeof(param_deg)) == ESP_OK) {
            servo_set_angle(atoi(param_deg));
            httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'deg' parameter");
    return ESP_FAIL;
}

static esp_err_t play_sound_handler(httpd_req_t *req) {
    char buf[100];
    int melody_id = 0;
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char param_id[10];
        if (httpd_query_key_value(buf, "id", param_id, sizeof(param_id)) == ESP_OK) {
            melody_id = atoi(param_id);
        }
    }
    buzzer_play_melody_async(melody_id);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_status = {.uri = "/status", .method = HTTP_GET, .handler = status_handler};
        httpd_uri_t uri_led = {.uri = "/set_led", .method = HTTP_GET, .handler = set_led_handler};
        httpd_uri_t uri_angle = {.uri = "/set_angle", .method = HTTP_GET, .handler = set_angle_handler};
        httpd_uri_t uri_sound = {.uri = "/play_sound", .method = HTTP_GET, .handler = play_sound_handler};

        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_led);
        httpd_register_uri_handler(server, &uri_angle);
        httpd_register_uri_handler(server, &uri_sound);
        
        ESP_LOGI(TAG, "HTTP Server active!");
    }
}