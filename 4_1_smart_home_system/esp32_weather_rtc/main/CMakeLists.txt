idf_component_register(
    SRCS "main.c" "ssd1306.c" "bmp280.c" "ds1307.c" "wifi_time.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2c esp_driver_uart esp_wifi esp_netif esp_event nvs_flash esp_timer lwip
)
