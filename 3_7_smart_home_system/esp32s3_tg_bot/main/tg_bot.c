#include "tg_bot.h"
#include "tg_config.h"
#include "actuator_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "clock_uart.h"
#include "esp_sntp.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "stdint.h"

static const char *TAG = "TG_BOT";
static int64_t last_update_id = 0;

#define MAX_ALLOWED_USERS 10
static int64_t allowed_users[MAX_ALLOWED_USERS] = {ADMIN_CHAT_ID};
static size_t allowed_users_count = 1;

static bool is_user_allowed(int64_t chat_id) {
    for (size_t i = 0; i < allowed_users_count; i++) {
        if (allowed_users[i] == chat_id) return true;
    }
    return false;
}

static bool add_allowed_user(int64_t chat_id) {
    if (is_user_allowed(chat_id)) return true;
    if (allowed_users_count < MAX_ALLOWED_USERS) {
        allowed_users[allowed_users_count++] = chat_id;
        return true;
    }
    return false;
}

typedef struct {
    char *buf;
    size_t max_len;
    size_t current_len;
} http_response_buffer_t;

static esp_err_t _http_response_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        http_response_buffer_t *res = (http_response_buffer_t *)evt->user_data;
        if (res && res->buf) {
            size_t copy_len = evt->data_len;
            if (res->current_len + copy_len >= res->max_len) {
                copy_len = res->max_len - res->current_len - 1;
            }
            if (copy_len > 0) {
                memcpy(res->buf + res->current_len, evt->data, copy_len);
                res->current_len += copy_len;
                res->buf[res->current_len] = '\0';
            }
        }
    }
    return ESP_OK;
}

void time_sync_init(void) {
    ESP_LOGI(TAG, "Initializing SNTP time sync...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();
}

static void scheduler_task(void *pvParameters) {
    (void)pvParameters;
    int last_active_state = -1; 
    time_t last_sync = 0;
    bool clock_synced_after_boot = false;

    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        // Перевіряємо чи час синхронізовано з інтернету (> 2020 року)
        if (timeinfo.tm_year > (2020 - 1900)) {
            
            // 1. Миттєва синхронізація годинника одразу після появи точного часу в інтернеті
            if (!clock_synced_after_boot) {
                vTaskDelay(pdMS_TO_TICKS(2000)); // невелика пауза на стабілізацію
                time(&now);
                localtime_r(&now, &timeinfo);
                clock_sync_time(&timeinfo);
                clock_synced_after_boot = true;
                last_sync = now;
                ESP_LOGI(TAG, "[UART CLOCK] Початкову синхронізацію часу з годинником виконано успішно (%02d:%02d:%02d)", 
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            }

            int current_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
            
            // Автоматичний розклад освітлення
            bool is_schedule_active = (current_min >= 5 * 60 && current_min < 7 * 60) ||
                                      (current_min >= 18 * 60 && current_min < 23 * 60);

            if ((int)is_schedule_active != last_active_state) {
                last_active_state = is_schedule_active;
                if (is_schedule_active) {
                    actuator_set_led(1, 4);
                    actuator_set_led(2, 4);
                    actuator_set_angle(90);
                    ESP_LOGI(TAG, "[SCHEDULE] Увімкнено вечірній/ранковий режим");
                } else {
                    actuator_set_led(1, 0);
                    actuator_set_led(2, 0);
                    actuator_set_angle(0);
                    ESP_LOGI(TAG, "[SCHEDULE] Вимкнено. Режим очікування");
                }
            }

            // 2. Періодична синхронізація часу з годинником через UART щогодини
            if (now - last_sync > 3600) {
                clock_sync_time(&timeinfo);
                last_sync = now;
                ESP_LOGI(TAG, "[UART CLOCK] Періодична синхронізація часу з годинником виконана");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // перевірка кожні 5 секунд
    }
}

static void send_tg_message(int64_t chat_id, const char* text, const char* reply_markup) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TG_BOT_TOKEN);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "parse_mode", "Markdown");

    if (reply_markup) {
        cJSON *markup = cJSON_Parse(reply_markup);
        if (markup) {
            cJSON_AddItemToObject(root, "reply_markup", markup);
        }
    }

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!post_data) return;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
        
        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send Telegram message: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
    }
    free(post_data);
}

static void send_main_menu(int64_t chat_id) {
    char status_raw[128] = {0};
    esp_err_t err = actuator_get_status(status_raw, sizeof(status_raw));

    char text[512];
    char markup[1024];

    int l[3] = {0};
    int deg = 0;
    
    clock_sensors_t sensors;
    bool has_sensors = clock_get_sensors(&sensors);
    char sensor_str[128] = "";
    if (has_sensors) {
        snprintf(sensor_str, sizeof(sensor_str), "\n• Температура: 🌡 **%.1f°C**\n• Вологість: 💧 **%.1f%%**", 
                 sensors.temperature, sensors.humidity);
    }

    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(status_raw);
        if (root) {
            cJSON *leds = cJSON_GetObjectItem(root, "leds");
            cJSON *angle = cJSON_GetObjectItem(root, "angle");

            if (leds && cJSON_IsArray(leds)) {
                for (int i = 0; i < 3 && i < cJSON_GetArraySize(leds); i++) {
                    cJSON *item = cJSON_GetArrayItem(leds, i);
                    if (item && cJSON_IsNumber(item)) {
                        l[i] = item->valueint;
                    }
                }
            }
            if (angle && cJSON_IsNumber(angle)) {
                deg = angle->valueint;
            }
            cJSON_Delete(root);

            snprintf(text, sizeof(text),
                "🏠 **Панель Розумного Будинку**\n"
                "• Статус актуатора: 🟢 **Онлайн**\n"
                "• Кут серво: **%d°**%s", deg, sensor_str);
        }
    } else {
        snprintf(text, sizeof(text), "🏠 **Панель Розумного Будинку**\n• Статус актуатора: 🔴 **Офлайн**%s", sensor_str);
    }

    // Клавіатура з 3 світлодіодами, майстер-вимикачами, серво та мелодіями
    snprintf(markup, sizeof(markup),
        "{\"inline_keyboard\":["
        "[{\"text\":\"LED 1 %s\",\"callback_data\":\"led_1\"},{\"text\":\"LED 2 %s\",\"callback_data\":\"led_2\"},{\"text\":\"LED 3 %s\",\"callback_data\":\"led_3\"}],"
        "[{\"text\":\"💡 Усі вимк.\",\"callback_data\":\"master_0\"},{\"text\":\"💡 Усі увімк.\",\"callback_data\":\"master_4\"}],"
        "[{\"text\":\"📐 0°\",\"callback_data\":\"ang_0\"},{\"text\":\"📐 90°\",\"callback_data\":\"ang_90\"},{\"text\":\"📐 180°\",\"callback_data\":\"ang_180\"}],"
        "[{\"text\":\"🎵 Гімн\",\"callback_data\":\"play_1\"},{\"text\":\"🎵 Підманула\",\"callback_data\":\"play_2\"}],"
        "[{\"text\":\"🎵 Eminem\",\"callback_data\":\"play_3\"},{\"text\":\"🎵 Morricone\",\"callback_data\":\"play_4\"}],"
        "[{\"text\":\"🎵 Pirates\",\"callback_data\":\"play_5\"},{\"text\":\"🔇 Стоп\",\"callback_data\":\"play_0\"}]"
        "]}",
        l[0] ? "🟢" : "⚪", l[1] ? "🟢" : "⚪", l[2] ? "🟢" : "⚪"
    );

    send_tg_message(chat_id, text, markup);
}

static void process_update(cJSON *update) {
    cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
    if (update_id && cJSON_IsNumber(update_id)) {
        last_update_id = (int64_t)update_id->valuedouble;
    }

    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (message) {
        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        cJSON *text = cJSON_GetObjectItem(message, "text");

        if (chat && text && cJSON_IsString(text)) {
            cJSON *id_item = cJSON_GetObjectItem(chat, "id");
            if (id_item && cJSON_IsNumber(id_item)) {
                int64_t chat_id = (int64_t)id_item->valuedouble;

                if (is_user_allowed(chat_id)) {
                    if (strcmp(text->valuestring, "/start") == 0 || strcmp(text->valuestring, "/menu") == 0) {
                        send_main_menu(chat_id);
                    }
                } else {
                    send_tg_message(chat_id, "🔒 Доступ обмежено. Запит на авторизацію відправлено адміністратору.", NULL);

                    cJSON *first_name = cJSON_GetObjectItem(chat, "first_name");
                    cJSON *username = cJSON_GetObjectItem(chat, "username");

                    const char *f_name = (first_name && cJSON_IsString(first_name)) ? first_name->valuestring : "Невідомо";
                    const char *u_name = (username && cJSON_IsString(username)) ? username->valuestring : "Немає";

                    char admin_msg[256];
                    snprintf(admin_msg, sizeof(admin_msg),
                             "⚠️ **Новий запит на доступ!**\n\n"
                             "ID: `%lld`\n"
                             "Ім'я: %s\n"
                             "Username: @%s",
                             chat_id, f_name, u_name);

                    char admin_markup[128];
                    snprintf(admin_markup, sizeof(admin_markup),
                             "{\"inline_keyboard\":[[{\"text\":\"✅ Надати доступ\",\"callback_data\":\"allow_%lld\"}]]}",
                             chat_id);

                    send_tg_message(ADMIN_CHAT_ID, admin_msg, admin_markup);
                }
            }
        }
    }

    cJSON *callback = cJSON_GetObjectItem(update, "callback_query");
    if (callback) {
        cJSON *from = cJSON_GetObjectItem(callback, "from");
        cJSON *data = cJSON_GetObjectItem(callback, "data");

        if (from && data && cJSON_IsString(data)) {
            cJSON *id_item = cJSON_GetObjectItem(from, "id");
            if (id_item && cJSON_IsNumber(id_item)) {
                int64_t chat_id = (int64_t)id_item->valuedouble;
                const char* cb_data = data->valuestring;

                if (is_user_allowed(chat_id)) {
                    if (strncmp(cb_data, "led_", 4) == 0) {
                        int led_id = atoi(cb_data + 4);
                        if (led_id < 1 || led_id > 3) {
                            return;
                        }
                        char status_raw[128] = {0};
                        int current_val = 0;
                        if (actuator_get_status(status_raw, sizeof(status_raw)) == ESP_OK) {
                            cJSON *st = cJSON_Parse(status_raw);
                            if (st) {
                                cJSON *leds = cJSON_GetObjectItem(st, "leds");
                                if (leds && cJSON_IsArray(leds) && (led_id - 1) < cJSON_GetArraySize(leds)) {
                                    cJSON *item = cJSON_GetArrayItem(leds, led_id - 1);
                                    if (item && cJSON_IsNumber(item)) current_val = item->valueint;
                                }
                                cJSON_Delete(st);
                            }
                        }
                        int new_val = (current_val > 0) ? 0 : 4;
                        actuator_set_led(led_id, new_val);
                        send_main_menu(chat_id);
                    } else if (strncmp(cb_data, "master_", 7) == 0) {
                        int level = atoi(cb_data + 7);
                        actuator_master_switch(level); // Обробка майстер-вимикача
                        send_main_menu(chat_id);
                    } else if (strncmp(cb_data, "ang_", 4) == 0) {
                        int deg = atoi(cb_data + 4);
                        actuator_set_angle(deg);
                        send_main_menu(chat_id);
                    } else if (strncmp(cb_data, "play_", 5) == 0) {
                        int m_id = atoi(cb_data + 5);
                        actuator_play_sound(m_id); 
                        send_main_menu(chat_id);
                    } else if (strncmp(cb_data, "allow_", 6) == 0 && chat_id == ADMIN_CHAT_ID) {
                        int64_t new_user_id = atoll(cb_data + 6);
                        if (add_allowed_user(new_user_id)) {
                            send_tg_message(ADMIN_CHAT_ID, "✅ Користувача успішно додано!", NULL);
                            send_tg_message(new_user_id, "🎉 Вам надано доступ до управління!", NULL);
                            send_main_menu(new_user_id);
                        } else {
                            send_tg_message(ADMIN_CHAT_ID, "❌ Список дозволених користувачів заповнений.", NULL);
                        }
                    }
                }
            }
        }
    }
}

static void tg_polling_task(void *pvParameters) {
    (void)pvParameters;

    char url[256];
    char *response_buf = malloc(4096);
    if (!response_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for polling buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        snprintf(url, sizeof(url),
                 "https://api.telegram.org/bot%s/getUpdates?offset=%lld&timeout=10",
                 TG_BOT_TOKEN, (long long)(last_update_id + 1));

        http_response_buffer_t res_buf = {
            .buf = response_buf,
            .max_len = 4096,
            .current_len = 0
        };

        esp_http_client_config_t config = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = _http_response_event_handler,
            .user_data = &res_buf,
            .timeout_ms = 15000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            if (res_buf.current_len > 0) {
                cJSON *root = cJSON_Parse(response_buf);
                if (root) {
                    cJSON *ok = cJSON_GetObjectItem(root, "ok");
                    cJSON *result = cJSON_GetObjectItem(root, "result");
                    if (ok && cJSON_IsTrue(ok) && result && cJSON_IsArray(result)) {
                        int size = cJSON_GetArraySize(result);
                        for (int i = 0; i < size; i++) {
                            process_update(cJSON_GetArrayItem(result, i));
                        }
                    }
                    cJSON_Delete(root);
                }
            }
        } else {
            ESP_LOGW(TAG, "Telegram polling failed: %s", esp_err_to_name(err));
        }
        esp_http_client_cleanup(client);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    free(response_buf);
}

void tg_bot_start(void) {
    time_sync_init();
    xTaskCreate(scheduler_task, "scheduler_task", 3072, NULL, 5, NULL);
    if (xTaskCreate(tg_polling_task, "tg_polling_task", 10240, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Telegram polling task");
    }
}