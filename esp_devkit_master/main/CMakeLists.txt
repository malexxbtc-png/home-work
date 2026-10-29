idf_component_register(
    SRCS "app_main.c" "status.c" "alerts.c" "stm_i2c.c" "ds1307.c" "ssd1306.c"
         "link_esp.c" "net_wx.c" "wx_json.c" "clock.c" "events.c" "triggers.c" "wx_data.c"
         "keys_logic.c" "keys_adc.c" "keys_cal.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2c esp_adc esp_timer
             nvs_flash freertos espressif__cjson
             esp_wifi esp_event esp_netif esp_http_client esp-tls mbedtls lwip)
