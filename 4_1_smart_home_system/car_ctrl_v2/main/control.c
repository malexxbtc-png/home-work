#include "control.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "joystick.h"
#include "motor_drive.h"
#include "steering.h"
#include "commands.h"
#include "joy_math.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG               "control"
#define CONTROL_PERIOD_MS 20

/* Знаки осей з menuconfig, щоб кермо збігалося з напрямком стіка */
#ifdef CONFIG_CAR_JOY_INVERT_X
#define JOY_X_SIGN (-1)
#else
#define JOY_X_SIGN (1)
#endif

#ifdef CONFIG_CAR_STEER_INVERT
#define STEER_SIGN (-1)
#else
#define STEER_SIGN (1)
#endif

static volatile bool s_active;

bool control_joystick_active(void)
{
    return s_active;
}

static void control_task(void *arg)
{
    joystick_state_t js;
    bool halt = false;

    for (;;) {
        if (joystick_read(&js) == ESP_OK) {
            /* Тяга пропорційна відхиленню Y, центр = 0; кнопка стіка зупиняє */
            motor_drive_set_percent(joy_motor_percent(js.y, js.pressed_edge, &halt));

#ifdef CONFIG_CAR_STEER_AXIS
            /* Кермо за віссю X: центр = 0°, повний відхил = ±CAR_STEER_MAX_DEG */
            steering_set_angle(joy_axis_angle_deg(js.x * JOY_X_SIGN,
                                                  CONFIG_CAR_STEER_MAX_DEG) * STEER_SIGN);
#else
            /* Кермо дивиться в напрямку вектора стіка; у центрі кут не змінюється */
            int angle;
            if (joy_vector_angle_deg(js.x * JOY_X_SIGN, js.y,
                                     CONFIG_CAR_STEER_VECTOR_MIN_PCT, &angle)) {
                steering_set_angle(angle * STEER_SIGN);
            }
#endif

            commands_feed_motor_watchdog();   /* джойстик «живить» failsafe */
        }
        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t control_start(void)
{
    const joystick_config_t jc = {
        .x_gpio   = CONFIG_CAR_JOY_X_GPIO,
        .y_gpio   = CONFIG_CAR_JOY_Y_GPIO,
        .sw_gpio  = CONFIG_CAR_JOY_SW_GPIO,
        .deadzone = CONFIG_CAR_JOY_DEADZONE,
#ifdef CONFIG_CAR_JOY_INVERT_Y
        .invert_y = true,
#else
        .invert_y = false,
#endif
    };
    esp_err_t err = joystick_init(&jc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "джойстик вимкнено: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(control_task, "control", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_active = true;
    return ESP_OK;
}
