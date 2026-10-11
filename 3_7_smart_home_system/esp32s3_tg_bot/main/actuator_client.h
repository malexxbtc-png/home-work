#ifndef ACTUATOR_CLIENT_H
#define ACTUATOR_CLIENT_H

#include "esp_http_client.h"
#include "esp_err.h"
#include "tg_config.h"

esp_err_t actuator_get_status(char* out_buf, size_t max_len);
esp_err_t actuator_set_led(int id, int level);
esp_err_t actuator_set_angle(int deg);
esp_err_t actuator_play_sound(int melody_id);
esp_err_t actuator_master_switch(int level); // Додано

#endif // ACTUATOR_CLIENT_H