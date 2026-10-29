idf_component_register(SRCS "forecast.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer freertos espressif__cjson)
