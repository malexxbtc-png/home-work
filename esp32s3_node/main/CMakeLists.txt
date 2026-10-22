idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES link_pico forecast display sd_card esp_wifi esp_timer nvs_flash espressif__cjson)
