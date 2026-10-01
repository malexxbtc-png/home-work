#include "tg_bot.h"
#include "tg_config.h"
#include "actuator_client.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "stdint.h"

static const char *TAG = "TG_BOT";
static int64_t last_update_id = 0;

// Динамічний список дозволених користувачів
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

static void send_tg_message(int64_t chat_id, const char* text, const char* reply_markup) {
    char url[256];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", TG_BOT_TOKEN);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "parse_mode", "HTML");

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

    char text[256];
    char markup[1024];

    int l[6] = {0};
    int deg = 0;

    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(status_raw);
        if (root) {
            cJSON *leds = cJSON_GetObjectItem(root, "leds");
            cJSON *angle = cJSON_GetObjectItem(root, "angle");

            if (leds && cJSON_IsArray(leds)) {
                for (int i = 0; i < 6 && i < cJSON_GetArraySize(leds); i++) {
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
                "• Статус: 🟢 **Онлайн**\n"
                "• Кут серво: **%d°**", deg);
        } else {
            snprintf(text, sizeof(text), "🏠 **Панель Розумного Будинку**\n• Статус: 🟢 Онлайн");
        }
    } else {
        snprintf(text, sizeof(text), "🏠 **Панель Розумного Будинку**\n• Статус: 🔴 **Офлайн**");
    }

    // Динамічне формування кнопок з індикаторами освітлення
    snprintf(markup, sizeof(markup),
        "{\"inline_keyboard\":["
        "[{\"text\":\"Кімн. 1 %s\",\"callback_data\":\"led_1\"},{\"text\":\"Кімн. 2 %s\",\"callback_data\":\"led_2\"}],"
        "[{\"text\":\"Кімн. 3 %s\",\"callback_data\":\"led_3\"},{\"text\":\"Кімн. 4 %s\",\"callback_data\":\"led_4\"}],"
        "[{\"text\":\"Кімн. 5 %s\",\"callback_data\":\"led_5\"},{\"text\":\"Кімн. 6 %s\",\"callback_data\":\"led_6\"}],"
        "[{\"text\":\"📐 0°\",\"callback_data\":\"ang_0\"},{\"text\":\"📐 90°\",\"callback_data\":\"ang_90\"},{\"text\":\"📐 180°\",\"callback_data\":\"ang_180\"}],"
        "[{\"text\":\"🎵 Мел. 1\",\"callback_data\":\"play_1\"},{\"text\":\"🎵 Мел. 2\",\"callback_data\":\"play_2\"},"
        "{\"text\":\"🎵 Мел. 3\",\"callback_data\":\"play_3\"},{\"text\":\"🎵 Мел. 4\",\"callback_data\":\"play_4\"}],"
        "[{\"text\":\"🔇 Стоп\",\"callback_data\":\"play_0\"}]"
        "]}",
        l[0] ? "🟢" : "⚪", l[1] ? "🟢" : "⚪",
        l[2] ? "🟢" : "⚪", l[3] ? "🟢" : "⚪",
        l[4] ? "🟢" : "⚪", l[5] ? "🟢" : "⚪"
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
                    // 1. Сповіщаємо користувача про запит
                    send_tg_message(chat_id, "🔒 Доступ обмежено. Запит на авторизацію відправлено адміністратору.", NULL);

                    // 2. Збираємо дані невідомого користувача
                    cJSON *first_name = cJSON_GetObjectItem(chat, "first_name");
                    cJSON *username = cJSON_GetObjectItem(chat, "username");

                    const char *f_name = (first_name && cJSON_IsString(first_name)) ? first_name->valuestring : "Невідомо";
                    const char *u_name = (username && cJSON_IsString(username)) ? username->valuestring : "Немає";

                    // 3. Формуємо сповіщення для адміна
                    char admin_msg[256];
                    snprintf(admin_msg, sizeof(admin_msg),
                             "⚠️ **Новий запит на доступ!**\n\n"
                             "ID: `%lld`\n"
                             "Ім'я: %s\n"
                             "Username: @%s",
                             chat_id, f_name, u_name);

                    // 4. Формуємо inline-кнопку для надання доступу
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
                        int id = atoi(cb_data + 4);
                        
                        char status_raw[128] = {0};
                        int current_lvl = 0;
                        if (actuator_get_status(status_raw, sizeof(status_raw)) == ESP_OK) {
                            cJSON *st = cJSON_Parse(status_raw);
                            if (st) {
                                cJSON *leds = cJSON_GetObjectItem(st, "leds");
                                if (leds && cJSON_IsArray(leds) && (id - 1) < cJSON_GetArraySize(leds)) {
                                    cJSON *elem = cJSON_GetArrayItem(leds, id - 1);
                                    if (elem && cJSON_IsNumber(elem)) {
                                        current_lvl = elem->valueint;
                                    }
                                }
                                cJSON_Delete(st);
                            }
                        }
                        int next_lvl = (current_lvl > 0) ? 0 : 4;
                        actuator_set_led(id, next_lvl);
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
                            // Сповіщаємо адміна та нового користувача
                            send_tg_message(ADMIN_CHAT_ID, "✅ Користувача успішно додано до списку дозволених.", NULL);
                            send_tg_message(new_user_id, "🎉 **Вітаємо!** Адміністратор надав вам доступ.\nНапишіть /menu для відкриття панелі.", NULL);
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
    if (xTaskCreate(tg_polling_task, "tg_polling_task", 10240, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Telegram polling task");
    }
}