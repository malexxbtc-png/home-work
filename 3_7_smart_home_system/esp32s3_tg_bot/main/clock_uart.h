#ifndef CLOCK_UART_H
#define CLOCK_UART_H

#include "esp_err.h"
#include "driver/uart.h"
#include "time.h"

typedef struct {
    float temperature;
    float humidity;
} clock_sensors_t;

esp_err_t clock_uart_init(void);
void clock_sync_time(struct tm *timeinfo);
bool clock_get_sensors(clock_sensors_t *sensors);

#endif // CLOCK_UART_H