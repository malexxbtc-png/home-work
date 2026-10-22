#include "power.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "gps.h"
#include "ui.h"
#include "wifi_time.h"

#define PIR_GPIO ((gpio_num_t)CONFIG_WS_PIR_GPIO)

static const char *TAG = "power";

void power_init(void)
{
    ESP_LOGI(TAG, "wakeup cause: %d", (int)esp_sleep_get_wakeup_cause());
}

bool power_enter_deep_sleep(void)
{
    if (gpio_get_level(PIR_GPIO)) {
        return false;
    }

    ESP_LOGI(TAG, "no motion, entering deep sleep");
    ui_sleep();
    gps_power(false);
    wifi_time_stop();

#if CONFIG_IDF_TARGET_ESP32
    /* Classic ESP32: ext1 on an RTC GPIO, wake on high level. */
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(1ULL << PIR_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_en(PIR_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_pullup_dis(PIR_GPIO));
#else
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(1ULL << PIR_GPIO,
                                                      ESP_GPIO_WAKEUP_GPIO_HIGH));
#endif

    esp_deep_sleep_start();
    return true; /* not reached */
}
