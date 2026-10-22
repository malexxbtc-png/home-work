idf_component_register(SRCS "forecast.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_http_client esp-tls mbedtls espressif__cjson esp_netif esp_timer lwip freertos)
