/*
 * Car control (ESP32-S3)
 *  - L298N + колекторний мотор: тяга (пропорційно джойстику або UART M:)
 *  - 28BYJ-48 + ULN2003: кермо (за вектором джойстика або UART S:)
 *  - MAX7219 8x8: стрілка, потужність у русі, телеметрія в спокої
 *  - UART1: телеметрія $SENS від погодної станції
 *  - UART2: команди M:/S: від ESP32 DevKit
 *  - джойстик (ADC1): тяга і кермо напряму
 */
#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "motor_drive.h"
#include "steering.h"
#include "max7219.h"
#include "uart_lines.h"
#include "telemetry.h"
#include "display.h"
#include "commands.h"
#include "control.h"
#include "pin_check.h"

static const char *TAG = "car";

/* Реверс є тільки з L298N. Для одного MOSFET IN-піни не використовуються. */
#ifdef CONFIG_CAR_MOTOR_REVERSE
#define MOTOR_IN1 CONFIG_CAR_L298_IN1_GPIO
#define MOTOR_IN2 CONFIG_CAR_L298_IN2_GPIO
#else
#define MOTOR_IN1 (-1)
#define MOTOR_IN2 (-1)
#endif

void app_main(void)
{
    /* Спершу перевіряємо піни: при конфлікті моторів не запускаємо */
    static const int pins[] = {
        CONFIG_CAR_L298_ENA_GPIO, MOTOR_IN1, MOTOR_IN2,
        CONFIG_CAR_STEER_IN1_GPIO, CONFIG_CAR_STEER_IN2_GPIO,
        CONFIG_CAR_STEER_IN3_GPIO, CONFIG_CAR_STEER_IN4_GPIO,
        CONFIG_CAR_MAX_MOSI_GPIO, CONFIG_CAR_MAX_CLK_GPIO, CONFIG_CAR_MAX_CS_GPIO,
        CONFIG_CAR_UART_TX_GPIO, CONFIG_CAR_UART_RX_GPIO,
        CONFIG_CAR_CMD_UART_TX_GPIO, CONFIG_CAR_CMD_UART_RX_GPIO,
#ifdef CONFIG_CAR_JOY_ENABLE
        CONFIG_CAR_JOY_X_GPIO, CONFIG_CAR_JOY_Y_GPIO, CONFIG_CAR_JOY_SW_GPIO,
#endif
    };
    static const char *const names[] = {
        "L298N ENA", "L298N IN1", "L298N IN2",
        "ULN2003 IN1", "ULN2003 IN2", "ULN2003 IN3", "ULN2003 IN4",
        "MAX7219 MOSI", "MAX7219 CLK", "MAX7219 CS",
        "UART telemetry TX", "UART telemetry RX",
        "UART cmd TX", "UART cmd RX",
#ifdef CONFIG_CAR_JOY_ENABLE
        "Joystick X", "Joystick Y", "Joystick SW",
#endif
    };
    _Static_assert(sizeof(pins) / sizeof(pins[0]) == sizeof(names) / sizeof(names[0]),
                   "pins і names мають однакову довжину");

    if (pin_check(pins, names, (int)(sizeof(pins) / sizeof(pins[0]))) != 0) {
        ESP_LOGE(TAG, "конфлікт пінів, виправте menuconfig. Мотори не запущено.");
        return;
    }

    ESP_ERROR_CHECK(telemetry_init());

    const motor_drive_config_t motor = {
        .ena_gpio     = CONFIG_CAR_L298_ENA_GPIO,
        .in1_gpio     = MOTOR_IN1,
        .in2_gpio     = MOTOR_IN2,
        .pwm_freq_hz  = CONFIG_CAR_PWM_FREQ_HZ,
        .min_duty_pct = CONFIG_CAR_MOTOR_MIN_DUTY_PCT,
        .max_duty_pct = CONFIG_CAR_MOTOR_MAX_DUTY_PCT,
    };
    ESP_ERROR_CHECK(motor_drive_init(&motor));

    const steering_config_t steer = {
        .gpio = {
            CONFIG_CAR_STEER_IN1_GPIO,
            CONFIG_CAR_STEER_IN2_GPIO,
            CONFIG_CAR_STEER_IN3_GPIO,
            CONFIG_CAR_STEER_IN4_GPIO,
        },
        .halfsteps_per_rev = CONFIG_CAR_STEER_HALFSTEPS_PER_REV,
        .max_angle_deg     = CONFIG_CAR_STEER_MAX_DEG,
        .step_delay_ms     = CONFIG_CAR_STEER_STEP_DELAY_MS,
    };
    ESP_ERROR_CHECK(steering_init(&steer));

    const max7219_config_t matrix = {
        .mosi_gpio = CONFIG_CAR_MAX_MOSI_GPIO,
        .clk_gpio  = CONFIG_CAR_MAX_CLK_GPIO,
        .cs_gpio   = CONFIG_CAR_MAX_CS_GPIO,
        .intensity = CONFIG_CAR_MATRIX_INTENSITY,
    };
    ESP_ERROR_CHECK(max7219_init(&matrix));
    ESP_ERROR_CHECK(display_start());

    /* Телеметрія від погодної станції ($SENS) */
    const uart_lines_config_t telem_uart = {
        .port    = CONFIG_CAR_UART_PORT,
        .baud    = CONFIG_CAR_UART_BAUD,
        .tx_gpio = CONFIG_CAR_UART_TX_GPIO,
        .rx_gpio = CONFIG_CAR_UART_RX_GPIO,
    };
    ESP_ERROR_CHECK(uart_lines_start(&telem_uart, commands_handle));

    /* Команди M:/S: від ESP32 DevKit (окремий UART) */
    const uart_lines_config_t cmd_uart = {
        .port    = CONFIG_CAR_CMD_UART_PORT,
        .baud    = CONFIG_CAR_CMD_UART_BAUD,
        .tx_gpio = CONFIG_CAR_CMD_UART_TX_GPIO,
        .rx_gpio = CONFIG_CAR_CMD_UART_RX_GPIO,
    };
    ESP_ERROR_CHECK(uart_lines_start(&cmd_uart, commands_handle));

    commands_start_failsafe();

#ifdef CONFIG_CAR_JOY_ENABLE
    if (control_start() != ESP_OK) {
        ESP_LOGE(TAG, "джойстик не працює, керування лише з UART");
    }
#endif

    ESP_LOGI(TAG, "ready");
}
