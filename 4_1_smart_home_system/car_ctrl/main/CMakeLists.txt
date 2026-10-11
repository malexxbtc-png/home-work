idf_component_register(
    SRCS "main.c" "commands.c" "display.c" "control.c" "joy_math.c" "pin_check.c"
    INCLUDE_DIRS "."
    REQUIRES motor_drive steering max7219 uart_lines telemetry joystick
    PRIV_REQUIRES freertos esp_timer
)
