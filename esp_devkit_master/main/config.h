#ifndef CONFIG_H
#define CONFIG_H

/* ===== Майстер: ESP32 DevKit (ESP-IDF) ===== */

#define BOOT_DELAY_S 3

/* --- I2C (внутрішні підтяжки + модулі) --- */
#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_MASTER_SDA_IO     21
#define I2C_MASTER_SCL_IO     22
#define I2C_MASTER_FREQ_HZ    100000

/* --- Піни --- */
#define PIN_ESP_TX   17        /* UART2 -> RX (GPIO18) ESP32-S3 */
#define PIN_ESP_RX   16        /* UART2 <- TX (GPIO17) ESP32-S3 */
#define PIN_PIR      23
#define PIN_KEYS_ADC 34        /* ADC1_CH6, лише вхід; резистивна клавіатура (ADKeyboard) */

/* --- Адреси I2C --- */
#define ADDR_SSD1306 0x3C
#define ADDR_DS1307  0x68
#define ADDR_STM32   0x30

/* --- Геолокація --- */
#define FIXED_LAT 49.9884
#define FIXED_LON 36.2328

/* --- Інтервали (с) --- */
#define STM_POLL_INTERVAL   2
#define HEARTBEAT_INTERVAL  5
#define PIR_COOLDOWN_S      30
#define ESP_OFFLINE_S       15
#define NODE_PUSH_INTERVAL_S 10     /* як часто слати вузлу погоду і час */
#define STM_HB_STALE_S      6

/* --- Годинник DS1307 --- */
#define RTC_MAX_DRIFT_S  2
#define VALID_EPOCH_MIN  1704067200LL

/* --- Камера: коли просити знімок у ESP32-S3 --- */
#define SNAP_ON_PIR            1      /* рух PIR */
#define SNAP_ON_ALERT          1      /* поява нової тривоги (wind, fog, ice, rain, snow) */
#define ALERT_SNAP_COOLDOWN_S  60     /* мінімум між автоматичними знімками */
#define SNAP_TIMEOUT_S         20     /* скільки чекати snap_done */

/* --- Журнал подій --- */
#define EVENT_LOG_SIZE 8

/* --- Резистивна клавіатура: номінальні напруги (мВ) за замовчуванням ---
   Живлення модуля 3,3 В! Точні значення ваш модуль може мати інші:
   утримуйте будь-яку кнопку при вмиканні — запуститься калібрування. */
#define KEY_NOM_RIGHT_MV   0
#define KEY_NOM_UP_MV      430
#define KEY_NOM_DOWN_MV    990
#define KEY_NOM_LEFT_MV    1540
#define KEY_NOM_SELECT_MV  2380
#define KEY_IDLE_MV        3100      /* напруга без натискання (до 3,3 В, ADC 12 дБ обрізає ~3,1 В) */
#define KEY_TOLERANCE_MAX  200
#define KEY_DEBOUNCE_MS    30
#define KEY_REPEAT_FIRST_MS 500
#define KEY_REPEAT_MS      120
#define KEY_SAMPLES        8         /* усереднення ADC */

/* --- Меню --- */
#define UI_IDLE_RETURN_S   30        /* повернення на головний екран */

/* --- Тривоги (погода з Open-Meteo: Wi-Fi на цій платі, див. net_wx.c) --- */
#define WX_MAX_AGE_MIN 120

#define WIND_AVG_ALERT  10.0
#define WIND_GUST_ALERT 15.0
#define WIND_HYST       1.0
#define RAIN_ALERT_MM   0.1
#define SNOW_ALERT_CM   0.1

#define FOG_RH_ON       95.0
#define FOG_RH_OFF      92.0
#define FOG_DEW_SPREAD  1.0

#define ICE_T_MIN -2.0
#define ICE_T_MAX 1.0
#define ICE_RH    85.0

#endif
