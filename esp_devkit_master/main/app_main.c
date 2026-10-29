#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "config.h"
#include "status.h"
#include "alerts.h"
#include "stm_i2c.h"
#include "ssd1306.h"
#include "link_esp.h"
#include "net_wx.h"
#include "wx_json.h"
#include "clock.h"
#include "events.h"
#include "triggers.h"
#include "wx_data.h"
#include "keys_adc.h"
#include "keys_cal.h"
#include "ui.h"

static const char *TAG = "app_main";

static i2c_master_bus_handle_t i2c_bus;
static ssd1306_handle_t oled;
static bool stm_ready;
static stm32_data_t stm_data;

static esp_err_t i2c_master_init(void)
{
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

static uint32_t ms_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------- опитування STM32 ---------- */
static void task_stm(void *arg)
{
    (void)arg;
    char err_buf[80];
    while (1) {
        esp_err_t err = stm_ready ? stm_i2c_read(&stm_data, err_buf, sizeof(err_buf)) : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            status.stm_fail = 0;
            status.t = stm_data.t;
            status.rh = stm_data.rh;
            status.p = stm_data.p;
            status.td = stm_data.td;
            status.stm_ok = true;
            ESP_LOGI(TAG, "STM32: T=%.2f C RH=%.2f%% P=%.2f hPa Td=%.2f C status=0x%02X HB=%u",
                     status.t, status.rh, status.p, status.td, stm_data.sensor_status, stm_data.heartbeat);
        } else {
            status.stm_fail++;
            if (status.stm_fail >= 3) {
                status.stm_ok = false;
            }
            ESP_LOGW(TAG, "STM32 data unavailable (%d in a row): %s", status.stm_fail,
                     stm_ready ? err_buf : "I2C device was not initialized");
        }
        vTaskDelay(pdMS_TO_TICKS(STM_POLL_INTERVAL * 1000));
    }
}

/* ---------- PIR, тривоги, події ---------- */

/* Чи є ім'я name у списку через кому csv */
static bool csv_has(const char *csv, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = csv; *p;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0) {
            return true;
        }
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static void update_from_hb(void)
{
    link_hb_t hb;
    if (link_esp_get_hb(&hb, ESP_OFFLINE_S * 1000)) {
        status.cam_ok = hb.cam;
        status.sd_ok = hb.sd;
        status.tg_ok = hb.tg;
    } else {
        status.cam_ok = status.sd_ok = status.tg_ok = -1;
    }
    status.wifi_rssi = net_wx_rssi();
    status.wx_age = net_wx_age_min();
}

static void task_sensors(void *arg)
{
    (void)arg;
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_PIR),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&io_conf);

    bool prev_pir = false;
    int64_t last_pir_event_us = INT64_MIN / 2;
    char prev_alerts[sizeof(status.active_alerts)] = "";
    int prev_stm = -1, prev_esp = -1;          /* -1 = ще не знаємо: перший стан не вважаємо подією */
    int64_t last_wx_parse_us = 0;
    wx_view_t wx = {0};

    trigger_event("boot", "power on", false);

    while (1) {
        int64_t now_us = esp_timer_get_time();
        status.esp_online = link_esp_is_online(ESP_OFFLINE_S * 1000);
        status.pir_state = gpio_get_level(PIN_PIR) == 1;
        update_from_hb();

        /* Погода з Open-Meteo (Wi-Fi на цій платі): оновлюємо раз на 2 с */
        if (now_us - last_wx_parse_us > 2000000) {
            last_wx_parse_us = now_us;
            static char json[1024];
            if (net_wx_get_json(json, sizeof(json)) && wx_parse(json, &wx)) {
                wx_cache_set(&wx);
            }
        }
        bool wx_valid = wx.valid && status.wx_age < WX_MAX_AGE_MIN;

        /* Тривоги */
        char alerts[sizeof(status.active_alerts)];
        float t = status.stm_ok ? status.t : -1000.0f;
        float rh = status.stm_ok ? status.rh : NAN;
        check_alerts(t, rh, status.td, wx_valid, wx.wind, wx.gust, wx.rain, wx.snow, alerts, sizeof(alerts));
        status_set_alerts(alerts);

        /* Нові тривоги -> подія зі знімком */
        static const char *const names[] = {"wind", "fog", "ice", "rain", "snow"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (csv_has(alerts, names[i]) && !csv_has(prev_alerts, names[i])) {
                trigger_event(names[i], "alert", SNAP_ON_ALERT);
            }
        }
        strcpy(prev_alerts, alerts);

        /* Рух */
        if (status.pir_state && !prev_pir &&
            (now_us - last_pir_event_us) >= (int64_t)PIR_COOLDOWN_S * 1000000) {
            last_pir_event_us = now_us;
            trigger_event("pir", "motion", SNAP_ON_PIR);
        }
        prev_pir = status.pir_state;

        /* Втрата і відновлення зв'язку */
        if (prev_stm >= 0 && prev_stm != (int)status.stm_ok) {
            trigger_event(status.stm_ok ? "stm_ok" : "stm_lost", "sensor", false);
        }
        prev_stm = status.stm_ok;
        if (prev_esp >= 0 && prev_esp != (int)status.esp_online) {
            trigger_event(status.esp_online ? "esp_ok" : "esp_lost", "link", false);
        }
        prev_esp = status.esp_online;

        triggers_poll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---------- обмін з ESP32-S3 ---------- */

/* Погода і час для екрана вузла: раз на NODE_PUSH_INTERVAL_S секунд */
static void push_wx_and_time(void)
{
    static char json[1024];
    if (net_wx_get_json(json, sizeof(json))) {
        link_esp_send_json(json);
    }
    int64_t epoch;
    if (net_wx_local_epoch(&epoch)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "{\"t\":\"time\",\"epoch\":%lld}", (long long)epoch);
        link_esp_send_json(msg);
    }
}

static void task_esp(void *arg)
{
    (void)arg;
    TickType_t last_send = 0;
    TickType_t last_push = 0;
    while (1) {
        if (last_push == 0 || (xTaskGetTickCount() - last_push) >= pdMS_TO_TICKS(NODE_PUSH_INTERVAL_S * 1000)) {
            last_push = xTaskGetTickCount();
            push_wx_and_time();
        }
        if (last_send == 0 || (xTaskGetTickCount() - last_send) >= pdMS_TO_TICKS(HEARTBEAT_INTERVAL * 1000)) {
            last_send = xTaskGetTickCount();
            system_status_t st;
            status_snapshot(&st);

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "t", "data");
            if (st.stm_ok) {
                cJSON_AddNumberToObject(root, "T", st.t);
                if (isfinite(st.rh)) cJSON_AddNumberToObject(root, "RH", st.rh);
                else cJSON_AddNullToObject(root, "RH");
                cJSON_AddNumberToObject(root, "P", st.p);
                if (st.td > -100.0f) cJSON_AddNumberToObject(root, "Td", st.td);
                else cJSON_AddNullToObject(root, "Td");
            } else {
                cJSON_AddNullToObject(root, "T");
                cJSON_AddNullToObject(root, "RH");
                cJSON_AddNullToObject(root, "P");
                cJSON_AddNullToObject(root, "Td");
            }
            cJSON *alerts = cJSON_CreateArray();
            char copy[sizeof(st.active_alerts)];
            snprintf(copy, sizeof(copy), "%s", st.active_alerts);
            for (char *tok = strtok(copy, ","); tok != NULL; tok = strtok(NULL, ",")) {
                cJSON_AddItemToArray(alerts, cJSON_CreateString(tok));
            }
            cJSON_AddItemToObject(root, "alert", alerts);
            cJSON *loc = cJSON_CreateArray();
            cJSON_AddItemToArray(loc, cJSON_CreateNumber(FIXED_LAT));
            cJSON_AddItemToArray(loc, cJSON_CreateNumber(FIXED_LON));
            cJSON_AddItemToObject(root, "loc", loc);
            cJSON *sys = cJSON_CreateObject();
            cJSON_AddNumberToObject(sys, "up", status_uptime_s());
            cJSON_AddNumberToObject(sys, "err", st.err_count);
            cJSON_AddNumberToObject(sys, "stm", st.stm_ok ? 1 : 0);
            cJSON_AddNumberToObject(sys, "pir", st.pir_state ? 1 : 0);
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

/* ---------- меню на OLED + клавіатура ---------- */
static void task_ui(void *arg)
{
    (void)arg;
    ui_t ui;
    ui_init(&ui, ms_now());
    uint32_t last_render = 0;

    while (1) {
        uint32_t now = ms_now();
        key_event_t ev = keys_adc_poll(now);
        bool redraw = false;
        if (ev.type != KEY_EV_NONE) {
            ui_action_t act = ui_on_key(&ui, ev, now);
            redraw = true;
            if (act == UI_ACT_PHOTO) {
                switch (trigger_manual_photo()) {
                case SNAP_REQ_SENT:    ui_toast(&ui, "Photo requested...", now, 2500); break;
                case SNAP_REQ_BUSY:    ui_toast(&ui, "Camera busy, wait", now, 2500); break;
                case SNAP_REQ_OFFLINE: ui_toast(&ui, "ESP32-S3 offline!", now, 2500); break;
                case SNAP_REQ_COOLDOWN: ui_toast(&ui, "Try again later", now, 2500); break;
                }
            }
        }
        ui_tick(&ui, now);

        if (oled != NULL && (redraw || (uint32_t)(now - last_render) >= 250)) {
            last_render = now;
            static ui_view_t view;
            memset(&view, 0, sizeof(view));
            view.time_ok = clock_get(&view.tm);
            status_snapshot(&view.st);
            wx_cache_get(&view.wx);
            view.key_mv = keys_adc_last_mv();
            view.key_now = keys_adc_current();
            view.cal = *keys_adc_cal();
            view.now_ms = now;
            ui_render(&ui, oled, &view);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    status_init();
    alert_manager_init();
    events_init();
    triggers_init();
    ESP_ERROR_CHECK(i2c_master_init());

    esp_err_t err = stm_i2c_init(i2c_bus, ADDR_STM32);
    stm_ready = err == ESP_OK;
    if (!stm_ready) {
        ESP_LOGE(TAG, "STM32 I2C setup failed: %s", esp_err_to_name(err));
    }

    clock_init(i2c_bus, ADDR_DS1307);

    err = ssd1306_init(i2c_bus, ADDR_SSD1306, &oled);
    if (err != ESP_OK) {
        oled = NULL;
        ESP_LOGW(TAG, "Continuing without SSD1306 display: %s", esp_err_to_name(err));
    }

    if (keys_adc_init() == ESP_OK) {
        keys_cal_run_if_requested(oled);
    } else {
        ESP_LOGE(TAG, "Keyboard ADC init failed; menu keys are disabled");
    }

    link_esp_init();
    net_wx_init();
    ESP_LOGI(TAG, "Starting Weather Station master on ESP32 DevKit");

    clock_start();
    net_wx_start();
    xTaskCreate(task_stm, "task_stm", 4096, NULL, 5, NULL);
    xTaskCreate(task_sensors, "task_sensors", 6144, NULL, 4, NULL);
    xTaskCreate(task_esp, "task_esp", 4096, NULL, 5, NULL);
    xTaskCreate(task_ui, "task_ui", 6144, NULL, 3, NULL);
}
