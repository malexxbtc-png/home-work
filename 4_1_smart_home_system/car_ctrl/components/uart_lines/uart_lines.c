#include <stdint.h>
#include <stdlib.h>
#include "uart_lines.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"

#define TAG                   "uart_lines"
#define UART_LINE_BUFFER_SIZE 128
#define RX_BUF                1024

typedef struct {
    uart_port_t     port;
    uart_lines_cb_t cb;
} uart_ctx_t;

static void rx_task(void *arg)
{
    const uart_ctx_t *ctx = (const uart_ctx_t *)arg;
    char    buf[UART_LINE_BUFFER_SIZE];
    size_t  len = 0;
    uint8_t ch;

    for (;;) {
        if (uart_read_bytes(ctx->port, &ch, 1, pdMS_TO_TICKS(100)) <= 0) {
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (len > 0) {
                buf[len] = '\0';
                ctx->cb(buf);
                len = 0;
            }
        } else if (len < UART_LINE_BUFFER_SIZE - 1) {
            buf[len++] = (char)ch;
        } else {
            ESP_LOGW(TAG, "рядок задовгий, відкинуто");
            len = 0;
        }
    }
}

esp_err_t uart_lines_start(const uart_lines_config_t *cfg, uart_lines_cb_t cb)
{
    if (!cfg || !cb) return ESP_ERR_INVALID_ARG;

    uart_ctx_t *ctx = calloc(1, sizeof(*ctx));   /* живе весь час роботи задачі */
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->port = (uart_port_t)cfg->port;
    ctx->cb   = cb;

    const uart_config_t uc = {
        .baud_rate  = cfg->baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(ctx->port, RX_BUF, 0, 0, NULL, 0), TAG, "install");
    ESP_RETURN_ON_ERROR(uart_param_config(ctx->port, &uc), TAG, "param");
    ESP_RETURN_ON_ERROR(uart_set_pin(ctx->port, cfg->tx_gpio, cfg->rx_gpio,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "pin");

    if (xTaskCreate(rx_task, "uart_rx", 3072, ctx, 6, NULL) != pdPASS) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
