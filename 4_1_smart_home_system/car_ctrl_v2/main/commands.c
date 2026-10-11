#include <stdlib.h>
#include "commands.h"
#include "sdkconfig.h"
#include "control.h"
#include "motor_drive.h"
#include "steering.h"
#include "telemetry.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "cmd"

static volatile int64_t s_last_motor_us;

void commands_feed_motor_watchdog(void)
{
    s_last_motor_us = esp_timer_get_time();
}

/* M: і S: з UART ігноруються, коли джойстик працює */
static void cmd_motor(const char *arg)
{
    if (control_joystick_active()) {
        ESP_LOGD(TAG, "M: ігнорується (джойстик)");
        return;
    }
    commands_feed_motor_watchdog();
    motor_drive_set_percent(atoi(arg));
}

static void cmd_steer(const char *arg)
{
    if (control_joystick_active()) {
        ESP_LOGD(TAG, "S: ігнорується (джойстик)");
        return;
    }
    steering_set_angle(atoi(arg));
}

void commands_handle(const char *line)
{
    if (line[0] == '$') {
        telemetry_update(line);
        return;
    }
    if (line[0] == '\0' || line[1] != ':') {
        ESP_LOGD(TAG, "невідомий рядок: %s", line);
        return;
    }
    switch (line[0]) {
        case 'M': cmd_motor(line + 2); break;
        case 'S': cmd_steer(line + 2); break;
        default:  ESP_LOGD(TAG, "невідома команда: %s", line); break;
    }
}

static void failsafe_task(void *arg)
{
    const int64_t limit_us = (int64_t)CONFIG_CAR_FAILSAFE_MS * 1000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (motor_drive_get_percent() != 0 &&
            esp_timer_get_time() - s_last_motor_us > limit_us) {
            motor_drive_set_percent(0);
            ESP_LOGW(TAG, "failsafe: тяга зупинена");
        }
    }
}

void commands_start_failsafe(void)
{
    commands_feed_motor_watchdog();
    if (xTaskCreate(failsafe_task, "failsafe", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити failsafe-задачу");
    }
}
