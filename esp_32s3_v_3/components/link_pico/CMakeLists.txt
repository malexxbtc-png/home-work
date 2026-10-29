idf_component_register(SRCS "link_pico.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_uart esp_timer freertos espressif__cjson)
