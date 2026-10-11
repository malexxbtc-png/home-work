#include "clock_uart.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "stdio.h"

static const char *TAG = "CLOCK_UART";

#define UART_PORT_NUM     UART_NUM_1
#define UART_TX_PIN       17  
#define UART_RX_PIN       16  
#define UART_BAUD_RATE    9600

esp_err_t clock_uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(UART_PORT_NUM, &uart_config);
    if (err != ESP_OK) return err;

    err = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    err = uart_driver_install(UART_PORT_NUM, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Clock UART initialized");
    return ESP_OK;
}

// Функція точної синхронізації часу з платою годинника
void clock_sync_time(struct tm *timeinfo) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "TIME:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min);
    uart_write_bytes(UART_PORT_NUM, buf, len);
    ESP_LOGI(TAG, "Sent sync time to clock: %02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
}

// Запит та зчитування температури з плати годинника (термістор R17)
bool clock_get_sensors(clock_sensors_t *sensors) {
    // Надсилаємо запит на отримання даних сенсора (якщо прошивка годинника реагує на "GET_TEMP\n")
    const char *req = "GET_TEMP\n";
    uart_write_bytes(UART_PORT_NUM, req, strlen(req));

    uint8_t data[64] = {0};
    int length = 0;
    
    // Коротка пауза на відповідь від плати годинника
    vTaskDelay(pdMS_TO_TICKS(50));
    
    esp_err_t err = uart_get_buffered_data_len(UART_PORT_NUM, (size_t*)&length);
    if (err == ESP_OK && length > 0) {
        int read_len = uart_read_bytes(UART_PORT_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(100));
        if (read_len > 0) {
            data[read_len] = '\0';
            ESP_LOGI(TAG, "UART Rx: %s", (char*)data);
            
            float temp = 0.0f;
            // Парсимо температуру від термістора плати годинника (формат наприклад "TEMP:24.5")
            if (sscanf((char*)data, "TEMP:%f", &temp) == 1 || sscanf((char*)data, "%f", &temp) == 1) {
                sensors->temperature = temp;
                sensors->humidity = 0.0f; // Вологість на цій платі відсутня
                return true;
            }
        }
    }
    return false;
}