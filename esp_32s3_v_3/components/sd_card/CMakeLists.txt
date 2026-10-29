idf_component_register(SRCS "sd_card.c"
                       INCLUDE_DIRS "include"
                       REQUIRES fatfs sdmmc vfs driver esp_driver_spi esp_timer freertos)
