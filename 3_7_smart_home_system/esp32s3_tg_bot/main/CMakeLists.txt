idf_component_register(
    SRCS "clock_uart.c" "main.c" "actuator_client.c" "tg_bot.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_http_client esp_wifi nvs_flash cjson esp-tls esp_netif lwip
)