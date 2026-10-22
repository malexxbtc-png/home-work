idf_component_register(SRCS "sd_card.c"
                    INCLUDE_DIRS "include"
                    REQUIRES fatfs sdmmc esp_driver_spi esp_timer)