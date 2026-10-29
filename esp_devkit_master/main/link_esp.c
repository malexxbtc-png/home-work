#include "link_esp.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "link_esp";

#define UART_RX_CHUNK  256
#define MAX_JSON       1024
#define MAX_LINE       (MAX_JSON + 8)       /* JSON + '*' + 4 hex + '\r' */

static int64_t last_rx_us;
static SemaphoreHandle_t link_mutex;
static SemaphoreHandle_t tx_mutex;
static char tx_buf[MAX_JSON + 8];
static link_hb_t hb_last;
static link_snap_done_t snap_done_last;
static bool snap_done_pending;

static uint16_t calculate_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void link_esp_send_json(const char *json_str)
{
    if (json_str == NULL || tx_mutex == NULL) {
        return;
    }
    size_t length = strlen(json_str);
    if (length == 0 || length >= MAX_JSON) {
        ESP_LOGE(TAG, "Message has invalid length %u", (unsigned)length);
        return;
    }
    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }
    uint16_t crc = calculate_crc16_ccitt((const uint8_t *)json_str, length);
    int n = snprintf(tx_buf, sizeof(tx_buf), "%s*%04X\n", json_str, crc);
    if (n > 0 && (size_t)n < sizeof(tx_buf)) {
        uart_write_bytes(UART_NUM_1, tx_buf, (size_t)n);
    }
    xSemaphoreGive(tx_mutex);
}

/* Перевіряє CRC, розбирає JSON і зберігає повідомлення за типом */
static void handle_line(char *line)
{
    char *star = strrchr(line, '*');
    if (star == NULL || strlen(star + 1) != 4) {
        return;
    }
    for (const char *p = star + 1; *p != '\0'; p++) {
        if (!isxdigit((unsigned char)*p)) {
            return;
        }
    }
    unsigned long received_crc = strtoul(star + 1, NULL, 16);
    *star = '\0';
    size_t body_length = strlen(line);
    if (body_length == 0 || body_length >= MAX_JSON ||
        calculate_crc16_ccitt((const uint8_t *)line, body_length) != received_crc) {
        ESP_LOGW(TAG, "Bad CRC or size, line dropped");
        return;
    }

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    int64_t now_us = esp_timer_get_time();

    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        last_rx_us = now_us;                               /* будь-яке коректне повідомлення = ESP на зв'язку */
        if (cJSON_IsString(type) && type->valuestring != NULL) {
            if (strcmp(type->valuestring, "hb") == 0) {
                const cJSON *cam = cJSON_GetObjectItemCaseSensitive(root, "cam");
                const cJSON *sd = cJSON_GetObjectItemCaseSensitive(root, "sd");
                const cJSON *tg = cJSON_GetObjectItemCaseSensitive(root, "tg");
                hb_last.cam = cJSON_IsNumber(cam) ? (int)cam->valuedouble : 0;
                hb_last.sd = cJSON_IsNumber(sd) ? (int)sd->valuedouble : 0;
                hb_last.tg = cJSON_IsNumber(tg) ? (int)tg->valuedouble : 0;
                hb_last.rx_us = now_us;
                hb_last.valid = true;
            } else if (strcmp(type->valuestring, "snap_done") == 0) {
                const cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
                const cJSON *file = cJSON_GetObjectItemCaseSensitive(root, "file");
                const cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "err");
                memset(&snap_done_last, 0, sizeof(snap_done_last));
                snap_done_last.ok = cJSON_IsTrue(ok);
                if (cJSON_IsString(file) && file->valuestring) {
                    snprintf(snap_done_last.file, sizeof(snap_done_last.file), "%s", file->valuestring);
                }
                if (cJSON_IsString(err) && err->valuestring) {
                    snprintf(snap_done_last.err, sizeof(snap_done_last.err), "%s", err->valuestring);
                }
                snap_done_pending = true;
            }
        }
        xSemaphoreGive(link_mutex);
    }
    cJSON_Delete(root);
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    static uint8_t data[UART_RX_CHUNK];
    static char line[MAX_LINE];
    size_t line_length = 0;
    bool dropping = false;

    while (1) {
        int len = uart_read_bytes(UART_NUM_1, data, sizeof(data), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            if (data[i] == '\n') {
                if (!dropping) {
                    if (line_length > 0 && line[line_length - 1] == '\r') {
                        line_length--;
                    }
                    line[line_length] = '\0';
                    if (line_length > 0) {
                        handle_line(line);
                    }
                }
                line_length = 0;
                dropping = false;
            } else if (!dropping) {
                if (line_length < sizeof(line) - 1) {
                    line[line_length++] = (char)data[i];
                } else {
                    dropping = true;                   /* занадто довгий рядок: відкидаємо до кінця */
                }
            }
        }
    }
}

void link_esp_init(void)
{
    link_mutex = xSemaphoreCreateMutex();
    tx_mutex = xSemaphoreCreateMutex();

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, PIN_ESP_TX, PIN_ESP_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 2048, 1536, 0, NULL, 0);

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);
}

bool link_esp_is_online(uint32_t timeout_ms)
{
    bool online = false;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        online = last_rx_us != 0 &&
                 (esp_timer_get_time() - last_rx_us) < (int64_t)timeout_ms * 1000;
        xSemaphoreGive(link_mutex);
    }
    return online;
}

bool link_esp_get_hb(link_hb_t *out, uint32_t max_age_ms)
{
    bool ok = false;
    if (out != NULL && xSemaphoreTake(link_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (hb_last.valid && (esp_timer_get_time() - hb_last.rx_us) < (int64_t)max_age_ms * 1000) {
            *out = hb_last;
            ok = true;
        }
        xSemaphoreGive(link_mutex);
    }
    return ok;
}

bool link_esp_take_snap_done(link_snap_done_t *out)
{
    bool ok = false;
    if (out != NULL && xSemaphoreTake(link_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (snap_done_pending) {
            *out = snap_done_last;
            snap_done_pending = false;
            ok = true;
        }
        xSemaphoreGive(link_mutex);
    }
    return ok;
}
