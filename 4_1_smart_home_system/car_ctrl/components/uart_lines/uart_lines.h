#pragma once
#include "esp_err.h"

/* Читає UART у фоновій задачі і викликає cb для кожного рядка (розділювачі \r або \n).
 * Можна викликати кілька разів для різних UART-портів. */
typedef struct {
    int port;
    int baud;
    int tx_gpio;      /* -1, якщо TX не використовується */
    int rx_gpio;
} uart_lines_config_t;

typedef void (*uart_lines_cb_t)(const char *line);

esp_err_t uart_lines_start(const uart_lines_config_t *cfg, uart_lines_cb_t cb);
