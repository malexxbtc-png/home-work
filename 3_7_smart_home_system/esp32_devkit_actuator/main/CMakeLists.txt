idf_component_register(
    SRCS "buzzer.c" "melodies.c" "main.c" "leds.c" "servo.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_wifi nvs_flash driver esp_driver_ledc esp_netif lwip
)