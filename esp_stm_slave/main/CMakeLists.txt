idf_component_register(SRCS "link_esp.c" "app_main.c" "status.c" "alerts.c" "stm_i2c.c" "mpu6050.c" "ds1307.c" "ssd1306.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2c esp_timer nvs_flash freertos cjson)