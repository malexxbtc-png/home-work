#pragma once
#ifndef CONFIG_H
#define CONFIG_H

// Безпечні піни для ESP32-S3
const int LED1_PIN = 1;
const int LED2_PIN = 19;
const int LED3_PIN = 21;

// Інтервали блимання (у мілісекундах)
const unsigned long LED1_INTERVAL = 400;
const unsigned long LED2_INTERVAL = 500;
const unsigned long LED3_INTERVAL = 1000;

#endif