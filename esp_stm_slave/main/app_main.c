#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include <stdint.h>
#include "config.h"
#include "status.h"
#include "alerts.h"
#include "stm_i2c.h"
#include "mpu6050.h"
#include "ds1307.h"
#include "ssd1306.h"
#include "link_esp.h"
#include "cJSON.h"

static const char *TAG = "app_main";

static i2c_master_bus_handle_t i2c_bus;
static ssd1306_handle_t oled;
static bool mpu_ready;
static bool stm_ready;
static bool rtc_ready;
static volatile bool mpu_ok;     
static stm32_data_t stm_data = {0};
static float current_tilt = 0.0f;
static float peak_shock = 0.0f;
static esp_err_t i2c_master_init(void) {
    const i2c_master_bus_config_t conf = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&conf, &i2c_bus);
}

static bool get_build_time(struct tm *time_info) {
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    char month_text[4] = {0};
    int year, day, hour, minute, second, month = -1;
    if (sscanf(__DATE__, "%3s %d %d", month_text, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_text, months[i]) == 0) {
            month = i;
            break;
        }
    }
    if (month < 0) {
        return false;
    }

    *time_info = (struct tm){
        .tm_year = year - 1900,
        .tm_mon = month,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = -1,
    };
    if (mktime(time_info) == (time_t)-1) {
        return false;
    }
    return true;
}


static void task_stm(void *arg) {
    char err_buf[80];
    while (1) {
        esp_err_t err = stm_ready
            ? stm_i2c_read(&stm_data, err_buf, sizeof(err_buf))
            : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            status.stm_fail = 0;
            status.stm_ok = true;
            status.t = stm_data.t;
            status.rh = stm_data.rh;
            status.p = stm_data.p;
            status.td = stm_data.td;
            ESP_LOGI(TAG, "STM32: T=%.2f C RH=%.2f%% P=%.2f hPa Td=%.2f C status=0x%02X raw_RH=0x%04X raw_TD=%d HB=%u",
                     status.t, status.rh, status.p, status.td, stm_data.sensor_status,
                     stm_data.raw_humidity, stm_data.raw_dewpoint, stm_data.heartbeat);
        } else {
            status.stm_fail++;
            if (status.stm_fail >= 3) {
                status.stm_ok = false;
            }
            ESP_LOGW(TAG, "STM32 data unavailable (%d consecutive failures): %s",
                     status.stm_fail,
                     stm_ready ? err_buf : "I2C device was not initialized");
        }
        vTaskDelay(pdMS_TO_TICKS(STM_POLL_INTERVAL * 1000));
    }
}

static void task_sensors(void *arg) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_PIR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    while (1) {
        float tilt = 0, shock = 0;
        mpu_ok = mpu_ready && mpu6050_get_accel_angles(&tilt, &shock) == ESP_OK;
        if (mpu_ok) {
            current_tilt = tilt;
            if (shock > peak_shock) peak_shock = shock;
        }

        status.pir_state = (gpio_get_level(PIN_PIR) == 1);

        char alert_buf[128];
        check_alerts(status.t, status.rh, status.td, current_tilt, peak_shock, false, 0, 0, 0, 0, alert_buf, sizeof(alert_buf));
        strcpy(status.active_alerts, alert_buf);

        peak_shock = 0.0f;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void task_esp(void *arg) {
    TickType_t last_send = xTaskGetTickCount();
    while (1) {
        status.esp_online = link_esp_is_online(ESP_OFFLINE_S * 1000);

        if ((xTaskGetTickCount() - last_send) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL * 1000)) {
            last_send = xTaskGetTickCount();
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "t", "data");
            if (status.stm_ok) {
                cJSON_AddNumberToObject(root, "T", status.t);
                if (isfinite(status.rh)) cJSON_AddNumberToObject(root, "RH", status.rh);
                else cJSON_AddNullToObject(root, "RH");
                cJSON_AddNumberToObject(root, "P", status.p);
                if (status.td > -100.0f) cJSON_AddNumberToObject(root, "Td", status.td);
                else cJSON_AddNullToObject(root, "Td");
            } else {
                cJSON_AddNullToObject(root, "T");
                cJSON_AddNullToObject(root, "RH");
                cJSON_AddNullToObject(root, "P");
                cJSON_AddNullToObject(root, "Td");
            }
            cJSON *alerts = cJSON_CreateArray();
            char alert_copy[sizeof(status.active_alerts)];
            snprintf(alert_copy, sizeof(alert_copy), "%s", status.active_alerts);
            for (char *tok = strtok(alert_copy, ","); tok != NULL; tok = strtok(NULL, ",")) {
                cJSON_AddItemToArray(alerts, cJSON_CreateString(tok));
            }
            cJSON_AddItemToObject(root, "alert", alerts);
            cJSON *loc = cJSON_CreateArray();
            cJSON_AddItemToArray(loc, cJSON_CreateNumber(FIXED_LAT));
            cJSON_AddItemToArray(loc, cJSON_CreateNumber(FIXED_LON));
            cJSON_AddItemToObject(root, "loc", loc);
            cJSON *sys = cJSON_CreateObject();
            cJSON_AddNumberToObject(sys, "up", status_uptime_s());
            cJSON_AddNumberToObject(sys, "err", status.err_count);
            cJSON_AddNumberToObject(sys, "stm", status.stm_ok ? 1 : 0);
            cJSON_AddItemToObject(root, "sys", sys);

            char *json_str = cJSON_PrintUnformatted(root);
            if (json_str) {
                link_esp_send_json(json_str);
                free(json_str);
            }
            cJSON_Delete(root);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Екран: верхня половина = час (великий) + дата і день тижня (малі),
   нижня половина = температура (велика) + статуси (малі) */
static void task_oled(void *arg) {
    static const char *const days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    struct tm tm_now = {0};
    bool rtc_ok = false;
    TickType_t last_rtc_read = 0;

    while (1) {
        if (oled == NULL) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        TickType_t now_ticks = xTaskGetTickCount();

        /* Годинник читаємо раз на секунду */
        if (last_rtc_read == 0 || (now_ticks - last_rtc_read) >= pdMS_TO_TICKS(1000)) {
            struct tm tmp = {0};
            rtc_ok = rtc_ready && ds1307_read_time(&tmp) == ESP_OK;
            if (rtc_ok) {
                tm_now = tmp;
                mktime(&tm_now);          /* обчислює правильний день тижня з дати */
            }
            last_rtc_read = now_ticks;
        }

        ssd1306_clear(oled, 0);
        char line[32];
        int w;

        /* --- Верхня половина: час великий, дата і день тижня малі --- */
        if (rtc_ok) {
            snprintf(line, sizeof(line), "%02d%c%02d", tm_now.tm_hour,
                     (tm_now.tm_sec % 2 == 0) ? ':' : ' ', tm_now.tm_min);
        } else {
            snprintf(line, sizeof(line), "--:--");
        }
        w = (int)strlen(line) * 6 * 3;
        ssd1306_draw_string_scaled(oled, (128 - w) / 2, 0, line, 3);

        if (rtc_ok) {
            snprintf(line, sizeof(line), "%02d.%02d.%04d %s", tm_now.tm_mday,
                     tm_now.tm_mon + 1, tm_now.tm_year + 1900, days[tm_now.tm_wday % 7]);
        } else {
            snprintf(line, sizeof(line), "--.--.---- ---");
        }
        ssd1306_draw_string(oled, (128 - (int)strlen(line) * 6) / 2, 24, line);

        /* --- Нижня половина: температура велика, статуси малі --- */
        if (status.stm_ok && isfinite(status.t)) {
            snprintf(line, sizeof(line), "%.1fC", status.t);
        } else {
            snprintf(line, sizeof(line), "--.-C");
        }
        w = (int)strlen(line) * 6 * 2;
        ssd1306_draw_string_scaled(oled, (128 - w) / 2, 34, line, 2);

        snprintf(line, sizeof(line), "STM %s ESP %s RTC %s",
                 status.stm_ok ? "OK" : "ER",
                 status.esp_online ? "ON" : "OFF",
                 rtc_ok ? "OK" : "ER");
        ssd1306_draw_string(oled, 0, 49, line);

        /* Другий рядок статусів: за наявності тривог чергується з MPU/PIR кожні 2 с */
        bool show_alert = status.active_alerts[0] && ((now_ticks / pdMS_TO_TICKS(2000)) % 2 == 0);
        if (show_alert) {
            snprintf(line, sizeof(line), "ALERT:%.15s", status.active_alerts);
        } else {
            snprintf(line, sizeof(line), "MPU %s PIR %s",
                     mpu_ok ? "OK" : "ER",
                     status.pir_state ? "MOTION" : "-");
        }
        ssd1306_draw_string(oled, 0, 57, line);

        ssd1306_show(oled);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* Unix-час з полів struct tm, трактованих як UTC (без часової зони) */
static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t tm_to_epoch(const struct tm *t) {
    return days_from_civil(t->tm_year + 1900, (unsigned)t->tm_mon + 1, (unsigned)t->tm_mday) * 86400 +
           t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

/* Коригує DS1307 за часом від ESP32-S3 ("майстра часу").
   ESP32-S3 шле {"t":"time","epoch":N} щохвилини; N = місцевий час (UTC + Київ, з літнім часом),
   тому годинник записуємо без додаткових зсувів. */
static void sync_rtc_from_master(void) {
    int64_t master_epoch;
    if (!rtc_ready || !link_esp_take_time(&master_epoch)) {
        return;
    }
    if (master_epoch < VALID_EPOCH_MIN) {
        return;
    }

    struct tm current;
    bool have_time = ds1307_read_time(&current) == ESP_OK;
    int64_t diff = have_time ? tm_to_epoch(&current) - master_epoch : INT64_MAX;
    if (have_time && diff >= -RTC_MAX_DRIFT_S && diff <= RTC_MAX_DRIFT_S) {
        strcpy(status.time_src, "esp");               /* годинник вже точний */
        return;
    }

    time_t t = (time_t)master_epoch;
    struct tm new_time;
    gmtime_r(&t, &new_time);
    if (ds1307_set_time(&new_time) == ESP_OK) {
        strcpy(status.time_src, "esp");
        if (have_time) {
            ESP_LOGI(TAG, "RTC corrected by %lld s from ESP32-S3 time", (long long)-diff);
        } else {
            ESP_LOGI(TAG, "RTC set from ESP32-S3 time");
        }
    } else {
        ESP_LOGW(TAG, "Failed to write corrected time to DS1307");
        status_log_error("rtc_sync");
    }
}

static void task_time_sync(void *arg) {
    while (1) {
        sync_rtc_from_master();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    status_init();
    alert_manager_init();
    ESP_ERROR_CHECK(i2c_master_init());
    esp_err_t err = stm_i2c_init(i2c_bus, ADDR_STM32);
    stm_ready = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STM32 I2C setup failed: %s", esp_err_to_name(err));
    }
    err = mpu6050_init(i2c_bus, ADDR_MPU6050);
    mpu_ready = err == ESP_OK;
    if (!mpu_ready) {
        ESP_LOGW(TAG, "Continuing without MPU6050");
    }
    err = ds1307_init(i2c_bus, ADDR_DS1307);
    rtc_ready = err == ESP_OK;
    if (rtc_ready) {
        struct tm rtc_time;
        err = ds1307_read_time(&rtc_time);
        time_t rtc_epoch = err == ESP_OK ? mktime(&rtc_time) : (time_t)-1;
        if (err == ESP_ERR_INVALID_RESPONSE ||
            (err == ESP_OK && rtc_epoch < (time_t)1704067200)) {
            if (get_build_time(&rtc_time)) {
                err = ds1307_set_time(&rtc_time);
                if (err == ESP_OK) {
                    ESP_LOGW(TAG, "RTC was unset; initialized from firmware build time");
                }
            }
        }
        if (err == ESP_OK) {
            strcpy(status.time_src, "rtc");
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DS1307 time unavailable: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "DS1307 not found: %s", esp_err_to_name(err));
    }
    err = ssd1306_init(i2c_bus, ADDR_SSD1306, &oled);
    if (err != ESP_OK) {
        oled = NULL;
        ESP_LOGW(TAG, "Continuing without SSD1306 display: %s", esp_err_to_name(err));
    }
    link_esp_init();

    ESP_LOGI(TAG, "Starting Weather Station on ESP32 DevKit");

    xTaskCreate(task_stm, "task_stm", 4096, NULL, 5, NULL);
    xTaskCreate(task_sensors, "task_sensors", 4096, NULL, 4, NULL);
    xTaskCreate(task_esp, "task_esp", 4096, NULL, 5, NULL);
    xTaskCreate(task_oled, "task_oled", 4096, NULL, 3, NULL);
    xTaskCreate(task_time_sync, "task_time_sync", 3072, NULL, 3, NULL);
}