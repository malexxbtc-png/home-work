#pragma once

/* Обробляє один рядок з UART:
 *   $SENS,...            -> телеметрія (показ на матриці в спокої)
 *   M:<-100..100>        -> тяга
 *   S:<-360..360>        -> кут керма, градуси
 * Коли джойстик працює, M: і S: ігноруються. */
void commands_handle(const char *line);

/* Зупиняє тягу, якщо немає команди руху довше CONFIG_CAR_FAILSAFE_MS. */
void commands_start_failsafe(void);

/* Викликається, коли рух ініційовано (з UART або джойстика). */
void commands_feed_motor_watchdog(void);
