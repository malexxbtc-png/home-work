idf_component_register(SRCS "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sd_card esp_timer freertos
                       PRIV_REQUIRES esp_driver_gpio)
