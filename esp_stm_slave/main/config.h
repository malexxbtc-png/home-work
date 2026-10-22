#ifndef CONFIG_H
#define CONFIG_H

#define WDT_ENABLED false
#define BOOT_DELAY_S 3
#define ALERT_SNAP_COOLDOWN_S 60

// Шина I2C на ESP32 DevKit
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_FREQ_HZ 100000

// Піни периферії
#define PIN_ESP_TX 17
#define PIN_ESP_RX 16
#define PIN_ESP_IRQ 4
#define PIN_PIR 23

// Адреси I2C пристроїв
#define ADDR_SSD1306 0x3C
#define ADDR_MPU6050 0x69
#define ADDR_DS1307  0x68
#define ADDR_STM32   0x30

// Геолокація
#define FIXED_LAT 49.9884
#define FIXED_LON 36.2328

// Інтервали (сек)
#define STM_POLL_INTERVAL 2
#define HEARTBEAT_INTERVAL 5
#define PIR_COOLDOWN_S 30
#define ESP_OFFLINE_S 15
#define STM_HB_STALE_S 6

// Годинник DS1307: коригуємо за часом від ESP32-S3, якщо розбіжність більша за це (с)
#define RTC_MAX_DRIFT_S 2
#define VALID_EPOCH_MIN 1704067200LL


#define TILT_LIMIT_DEG 90.0
#define SHOCK_THRESHOLD 5
#define WX_MAX_AGE_MIN 120

#define WIND_AVG_ALERT 10.0
#define WIND_GUST_ALERT 15.0
#define WIND_HYST 1.0
#define RAIN_ALERT_MM 0.1
#define SNOW_ALERT_CM 0.1

#define FOG_RH_ON 95.0
#define FOG_RH_OFF 92.0
#define FOG_DEW_SPREAD 1.0

#define ICE_T_MIN -2.0
#define ICE_T_MAX 1.0
#define ICE_RH 85.0

#endif