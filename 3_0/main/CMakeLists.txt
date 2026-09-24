idf_component_register(SRCS "main.c" "hardware.c" "sma_filter.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_adc)