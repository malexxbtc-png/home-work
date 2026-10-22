#include "uart_out.h"

#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

#define LINK_UART UART_NUM_2

static const char *TAG = "uart_out";

esp_err_t uart_out_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = CONFIG_WS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 256, 512, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    return uart_set_pin(LINK_UART, CONFIG_WS_UART_TX, CONFIG_WS_UART_RX,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/* body = text between '$' and '*' (exclusive); sends "$body*XX\r\n" with XOR checksum. */
static void send_sentence(const char *body)
{
    uint8_t sum = 0;
    for (const char *p = body; *p; p++) {
        sum ^= (uint8_t)*p;
    }
    char out[160];
    int n = snprintf(out, sizeof(out), "$%s*%02X\r\n", body, sum);
    if (n > 0 && n < (int)sizeof(out)) {
        uart_write_bytes(LINK_UART, out, (size_t)n);
    } else {
        ESP_LOGW(TAG, "sentence too long, dropped");
    }
}

void uart_out_sensors(float temp_c, bool has_humidity, float humidity, float pressure_hpa)
{
    char body[96];
    if (has_humidity) {
        snprintf(body, sizeof(body), "SENS,%.2f,%.1f,%.2f", temp_c, humidity, pressure_hpa);
    } else {
        snprintf(body, sizeof(body), "SENS,%.2f,NA,%.2f", temp_c, pressure_hpa);
    }
    send_sentence(body);
}

void uart_out_extra(const station_state_t *s)
{
#if CONFIG_WS_UART_EXTRA
    char body[96];
    if (s->geo_valid) {
        snprintf(body, sizeof(body), "GEO,%.5f,%.5f,%s", s->lat, s->lon,
                 s->city[0] ? s->city : "NA");
        send_sentence(body);
    }
    if (s->has_forecast) {
        for (int i = 0; i < FORECAST_DAYS; i++) {
            snprintf(body, sizeof(body), "FCST,%d,%s,%.1f,%.1f,%d", i, s->days[i].date,
                     s->days[i].tmin, s->days[i].tmax, s->days[i].wmo_code);
            send_sentence(body);
        }
    }
#else
    (void)s;
#endif
}
