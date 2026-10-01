idf_component_register(
    SRCS "main.c" "actuator_client.c" "tg_bot.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client esp_wifi nvs_flash cjson esp-tls esp_netif
)