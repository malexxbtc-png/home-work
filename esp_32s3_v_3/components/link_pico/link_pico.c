#include "link_pico.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "link_pico";

#define UART_NUM       UART_NUM_1
#define MAX_JSON       CONFIG_WEATHER_LINK_PICO_MAX_JSON_SIZE
#define MAX_LINE       (MAX_JSON + 8)       /* JSON + '*' + 4 hex + '\r' + запас */
#define MUTEX_TICKS    pdMS_TO_TICKS(CONFIG_WEATHER_LINK_PICO_MUTEX_TIMEOUT_MS)

static char latest_data_json[MAX_JSON];
static char latest_snap_json[MAX_JSON];
static int64_t latest_data_received_us;
static SemaphoreHandle_t data_mutex;
static SemaphoreHandle_t tx_mutex;
static char tx_buf[MAX_JSON + 8];

static uint16_t calculate_crc16(const uint8_t *data, size_t len)
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

/* Перевіряє CRC, розбирає JSON і зберігає повідомлення типу "data" або "snap" */
static bool store_received_line(char *line)
{
  char *crc_separator = strrchr(line, '*');
  if (crc_separator == NULL || strlen(crc_separator + 1) != 4) {
    ESP_LOGW(TAG, "Ignoring UART line without a four-digit CRC");
    return false;
  }
  for (const char *digit = crc_separator + 1; *digit != '\0'; digit++) {
    if (!isxdigit((unsigned char)*digit)) {
      ESP_LOGW(TAG, "Ignoring UART line with an invalid CRC");
      return false;
    }
  }

  unsigned long received_crc = strtoul(crc_separator + 1, NULL, 16);
  *crc_separator = '\0';
  size_t body_length = strlen(line);
  if (body_length == 0 || body_length >= MAX_JSON ||
      calculate_crc16((const uint8_t *)line, body_length) != received_crc) {
    ESP_LOGW(TAG, "Ignoring UART line with a bad CRC or size");
    return false;
  }

  cJSON *root = cJSON_Parse(line);
  if (root == NULL) {
    ESP_LOGW(TAG, "Ignoring invalid JSON received from Pico");
    return false;
  }

  const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
  char *destination = NULL;
  if (cJSON_IsString(type) && type->valuestring != NULL) {
    if (strcmp(type->valuestring, "data") == 0) {
      destination = latest_data_json;
    } else if (strcmp(type->valuestring, "snap") == 0) {
      destination = latest_snap_json;
    }
  }

  if (destination != NULL && xSemaphoreTake(data_mutex, MUTEX_TICKS) == pdTRUE) {
    memcpy(destination, line, body_length + 1);
    if (destination == latest_data_json) {
      latest_data_received_us = esp_timer_get_time();
    }
    xSemaphoreGive(data_mutex);
  }
  cJSON_Delete(root);
  return destination != NULL;
}

static void uart_rx_task(void *arg)
{
  (void)arg;
  static uint8_t data[256];
  static char line[MAX_LINE];
  size_t line_length = 0;
  bool dropping_line = false;

  while (1) {
    int len = uart_read_bytes(UART_NUM, data, sizeof(data),
                              pdMS_TO_TICKS(CONFIG_WEATHER_LINK_PICO_RX_TIMEOUT_MS));
    for (int i = 0; i < len; i++) {
      if (data[i] == '\n') {
        if (!dropping_line) {
          if (line_length > 0 && line[line_length - 1] == '\r') {
            line_length--;
          }
          line[line_length] = '\0';
          if (line_length > 0) {
            store_received_line(line);
          }
        } else {
          ESP_LOGW(TAG, "Discarded oversized UART line");
        }
        line_length = 0;
        dropping_line = false;
      } else if (!dropping_line) {
        if (line_length < sizeof(line) - 1) {
          line[line_length++] = (char)data[i];
        } else {
          dropping_line = true;
        }
      }
    }
  }
}

void link_pico_init(void)
{
  data_mutex = xSemaphoreCreateMutex();
  tx_mutex = xSemaphoreCreateMutex();
  if (data_mutex == NULL || tx_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create Pico link mutexes");
    return;
  }

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << CONFIG_WEATHER_LINK_PICO_IRQ_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_set_level(CONFIG_WEATHER_LINK_PICO_IRQ_PIN, 0));

  uart_config_t uart_config = {
      .baud_rate = CONFIG_WEATHER_LINK_PICO_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM, CONFIG_WEATHER_LINK_PICO_UART_TX_PIN,
                               CONFIG_WEATHER_LINK_PICO_UART_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM, CONFIG_WEATHER_LINK_PICO_UART_BUFFER_SIZE,
                                      CONFIG_WEATHER_LINK_PICO_TX_BUFFER_SIZE, 0, NULL, 0));

  if (xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create Pico UART RX task");
  }
}

void link_pico_trigger_irq(void)
{
  gpio_set_level(CONFIG_WEATHER_LINK_PICO_IRQ_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(CONFIG_WEATHER_LINK_PICO_IRQ_PULSE_MS));
  gpio_set_level(CONFIG_WEATHER_LINK_PICO_IRQ_PIN, 0);
}

bool link_pico_send_json(const char *json_str)
{
  if (json_str == NULL || tx_mutex == NULL) {
    return false;
  }
  size_t json_length = strlen(json_str);
  if (json_length == 0 || json_length >= MAX_JSON) {
    ESP_LOGE(TAG, "Pico UART message has invalid length %u", (unsigned)json_length);
    return false;
  }

  bool ok = false;
  if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGE(TAG, "UART transmit lock timeout");
    return false;
  }

  uint16_t crc = calculate_crc16((const uint8_t *)json_str, json_length);
  int length = snprintf(tx_buf, sizeof(tx_buf), "%s*%04X\n", json_str, crc);
  if (length > 0 && (size_t)length < sizeof(tx_buf)) {
    int written = uart_write_bytes(UART_NUM, tx_buf, (size_t)length);
    if (written != length) {
      ESP_LOGE(TAG, "UART write incomplete: %d of %d bytes", written, length);
    } else if (uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(200)) != ESP_OK) {
      ESP_LOGE(TAG, "UART transmission did not finish");
    } else {
      ok = true;
    }
  }
  xSemaphoreGive(tx_mutex);

  if (ok) {
    link_pico_trigger_irq();
  }
  return ok;
}

static bool copy_message(const char *source, char *dest, size_t max_len)
{
  if (source[0] == '\0' || dest == NULL || max_len == 0) {
    return false;
  }
  size_t length = strnlen(source, max_len - 1);
  memcpy(dest, source, length);
  dest[length] = '\0';
  return true;
}

bool link_pico_get_latest_data(char *dest, size_t max_len, uint32_t *age_ms)
{
  if (data_mutex == NULL || dest == NULL || max_len == 0) {
    return false;
  }
  bool found = false;
  if (xSemaphoreTake(data_mutex, MUTEX_TICKS) == pdTRUE) {
    found = copy_message(latest_data_json, dest, max_len);
    if (found && age_ms != NULL) {
      *age_ms = (uint32_t)((esp_timer_get_time() - latest_data_received_us) / 1000);
    }
    xSemaphoreGive(data_mutex);
  }
  return found;
}

bool link_pico_get_latest_snap(char *dest, size_t max_len)
{
  if (data_mutex == NULL || dest == NULL || max_len == 0) {
    return false;
  }
  bool found = false;
  if (xSemaphoreTake(data_mutex, MUTEX_TICKS) == pdTRUE) {
    found = copy_message(latest_snap_json, dest, max_len);
    if (found) {
      latest_snap_json[0] = '\0';
    }
    xSemaphoreGive(data_mutex);
  }
  return found;
}

bool link_pico_data_is_fresh(void)
{
  uint32_t age_ms = 0;
  /* Потрібен лише вік: копіюємо мінімум даних */
  if (data_mutex == NULL || xSemaphoreTake(data_mutex, MUTEX_TICKS) != pdTRUE) {
    return false;
  }
  bool found = latest_data_json[0] != '\0';
  if (found) {
    age_ms = (uint32_t)((esp_timer_get_time() - latest_data_received_us) / 1000);
  }
  xSemaphoreGive(data_mutex);
  return found && age_ms <= CONFIG_WEATHER_LINK_PICO_DATA_STALE_TIMEOUT_MS;
}
