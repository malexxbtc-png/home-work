idf_component_register(SRCS "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_timer freertos lvgl__lvgl espressif__cjson link_pico forecast)
