#ifndef NET_WX_H
#define NET_WX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wi-Fi, SNTP і запити до Open-Meteo на ESP32 DevKit (раніше це робив ESP32-S3) */

/* Задає часову зону. Викликати до запуску задач. */
void net_wx_init(void);

/* Запускає задачу Wi-Fi/SNTP/Open-Meteo. Порожній SSID у menuconfig = мережа вимкнена. */
void net_wx_start(void);

/* Повідомлення "wx" для UART і розбору (див. wx_json.h). false, якщо даних немає або вони застарілі */
bool net_wx_get_json(char *dest, size_t size);

/* Вік погоди в хвилинах, 999 = даних немає */
int net_wx_age_min(void);

/* RSSI точки доступу, -127 = немає зв'язку */
int net_wx_rssi(void);

/* Місцевий «настінний» Unix-час після синхронізації SNTP. false, якщо час ще не синхронізовано */
bool net_wx_local_epoch(int64_t *epoch_local);

#endif
