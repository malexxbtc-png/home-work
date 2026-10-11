#pragma once
#include <stdbool.h>

/* Кут керма з вектора стіка, градуси: 0 = вперед (вгору), +90 = вправо, -90 = вліво.
 * x_pct, y_pct: -100..100. Повертає false, якщо вектор коротший за min_mag_pct
 * (стік близько центру) — тоді кут не змінюємо. Результат у -180..180. */
bool joy_vector_angle_deg(int x_pct, int y_pct, int min_mag_pct, int *angle_deg);

/* Кермо за віссю X: кут пропорційний відхиленню, центр = 0°.
 * x_pct: -100..100, max_deg: повний відхил. Результат у -max_deg..max_deg. */
int joy_axis_angle_deg(int x_pct, int max_deg);

/* Тяга з осі Y, -100..100. Пропорційна: центр = 0, відхилення дає тягу з того ж знаку.
 * Мінімальну шпаруватість старту додає motor_drive.
 * pressed_edge (кнопка стіка) зупиняє тягу. Тяга не повертається, доки стік не в центрі.
 * halt — стан між викликами, початково false. */
int joy_motor_percent(int y_pct, bool pressed_edge, bool *halt);
