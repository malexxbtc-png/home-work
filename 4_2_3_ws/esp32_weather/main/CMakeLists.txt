idf_component_register(
    SRCS "main.c" "i2c_bus.c" "ssd1306.c" "bmp280.c" "ds1307.c" "timeutil.c"
         "clock_sync.c" "wifi_time.c" "gps.c" "weather.c" "motion.c" "power.c"
         "uart_out.c" "ui.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_client esp-tls
                  mbedtls esp_timer lwip
                  esp_driver_i2c esp_driver_gpio esp_driver_uart
)
