#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sd_card.h"

#if CONFIG_WEATHER_CAMERA_ENABLED
#include "esp_camera.h"
#include "img_converters.h"
#endif

static const char *TAG = "camera";

/* Вимкнені bool-опції Kconfig в sdkconfig.h не визначені взагалі */
#ifdef CONFIG_WEATHER_CAM_HFLIP
#define CAM_HFLIP 1
#else
#define CAM_HFLIP 0
#endif
#ifdef CONFIG_WEATHER_CAM_VFLIP
#define CAM_VFLIP 1
#else
#define CAM_VFLIP 0
#endif

#define VALID_EPOCH_MIN 1704067200LL
#define REINIT_INTERVAL_US (30LL * 1000000)

static bool cam_ready;      /* драйвер зараз ініціалізований (XCLK іде) */
static bool cam_seen;       /* камеру хоч раз успішно знайдено */
static int64_t last_init_attempt_us = -REINIT_INTERVAL_US;

bool camera_is_ready(void)
{
    return cam_seen;
}

#if CONFIG_WEATHER_CAMERA_ENABLED

static bool cam_start(void)
{
    if (cam_ready) {
        return true;
    }
    last_init_attempt_us = esp_timer_get_time();

    camera_config_t config = {
        .pin_pwdn = CONFIG_WEATHER_CAM_PIN_PWDN,
        .pin_reset = CONFIG_WEATHER_CAM_PIN_RESET,
        .pin_xclk = CONFIG_WEATHER_CAM_PIN_XCLK,
        .pin_sccb_sda = CONFIG_WEATHER_CAM_PIN_SIOD,
        .pin_sccb_scl = CONFIG_WEATHER_CAM_PIN_SIOC,
        .pin_d7 = CONFIG_WEATHER_CAM_PIN_D7,
        .pin_d6 = CONFIG_WEATHER_CAM_PIN_D6,
        .pin_d5 = CONFIG_WEATHER_CAM_PIN_D5,
        .pin_d4 = CONFIG_WEATHER_CAM_PIN_D4,
        .pin_d3 = CONFIG_WEATHER_CAM_PIN_D3,
        .pin_d2 = CONFIG_WEATHER_CAM_PIN_D2,
        .pin_d1 = CONFIG_WEATHER_CAM_PIN_D1,
        .pin_d0 = CONFIG_WEATHER_CAM_PIN_D0,
        .pin_vsync = CONFIG_WEATHER_CAM_PIN_VSYNC,
        .pin_href = CONFIG_WEATHER_CAM_PIN_HREF,
        .pin_pclk = CONFIG_WEATHER_CAM_PIN_PCLK,
        .xclk_freq_hz = CONFIG_WEATHER_CAM_XCLK_MHZ * 1000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        /* OV7670 не вміє JPEG: беремо RGB565 і кодуємо програмно (frame2jpg) */
        .pixel_format = PIXFORMAT_RGB565,
#if CONFIG_WEATHER_CAM_FRAME_QVGA
        .frame_size = FRAMESIZE_QVGA,
#else
        .frame_size = FRAMESIZE_VGA,
#endif
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = 1,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x (check wiring, 3V3 supply, SIOD/SIOC pull-ups)", err);
        return false;
    }
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        if (sensor->set_hmirror) sensor->set_hmirror(sensor, CAM_HFLIP);
        if (sensor->set_vflip) sensor->set_vflip(sensor, CAM_VFLIP);
        ESP_LOGI(TAG, "Sensor PID 0x%02x", sensor->id.PID);
    }
    cam_ready = true;
    cam_seen = true;
    ESP_LOGI(TAG, "Camera ready");
    return true;
}

/* Зупиняє XCLK і DMA камери. Тактовий сигнал 10 МГц наводить завади на SD (спільні довгі дроти),
   тому за замовчуванням камера працює лише під час знімка. */
static void cam_stop(void)
{
#if CONFIG_WEATHER_CAM_POWER_DOWN
    if (cam_ready) {
        esp_camera_deinit();
        cam_ready = false;
    }
#endif
}

bool camera_init(void)
{
    bool ok = cam_start();
    if (ok) {
        cam_stop();            /* на старті лише перевірка, що камера на місці */
    }
    return ok;
}

static void sanitize_reason(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (; in != NULL && *in != '\0' && n + 1 < size && n < 10; in++) {
        char c = *in;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[n++] = ok ? c : '_';
    }
    out[n] = '\0';
    if (n == 0) {
        strcpy(out, "snap");
    }
}

static void fail(camera_result_t *r, const char *err)
{
    r->ok = false;
    snprintf(r->err, sizeof(r->err), "%s", err);
}

void camera_snap(const char *reason, camera_result_t *result)
{
    memset(result, 0, sizeof(*result));

    if (!sd_card_is_ready() && !sd_card_init()) {
        fail(result, "no_sd");
        return;
    }
    if (!cam_ready) {
        if ((!cam_seen && (esp_timer_get_time() - last_init_attempt_us) < REINIT_INTERVAL_US) || !cam_start()) {
            fail(result, "no_camera");
            return;
        }
    }

    /* Кадри, зняті під час налаштування експозиції, відкидаємо */
    for (int i = 0; i < CONFIG_WEATHER_CAM_WARMUP_FRAMES; i++) {
        camera_fb_t *warm = esp_camera_fb_get();
        if (warm != NULL) {
            esp_camera_fb_return(warm);
        }
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "Frame capture failed");
        cam_stop();
        fail(result, "capture");
        return;
    }

#if CONFIG_WEATHER_CAM_SWAP_BYTES
    if (fb->format == PIXFORMAT_RGB565) {
        for (size_t i = 0; i + 1 < fb->len; i += 2) {
            uint8_t tmp = fb->buf[i];
            fb->buf[i] = fb->buf[i + 1];
            fb->buf[i + 1] = tmp;
        }
    }
#endif

    uint8_t *jpeg = NULL;
    size_t jpeg_len = 0;
    bool encoded = frame2jpg(fb, CONFIG_WEATHER_CAM_JPEG_QUALITY, &jpeg, &jpeg_len);
    esp_camera_fb_return(fb);
    cam_stop();                /* далі лише робота з SD: XCLK вимкнений */
    if (!encoded || jpeg == NULL || jpeg_len == 0) {
        ESP_LOGE(TAG, "JPEG encoding failed");
        free(jpeg);
        fail(result, "encode");
        return;
    }

    char tag[12];
    sanitize_reason(reason, tag, sizeof(tag));
    char dir[24], name[48];
    time_t now = time(NULL);
    if (now >= VALID_EPOCH_MIN) {
        struct tm lt;
        localtime_r(&now, &lt);
        /* беззнакові значення з %: компілятор бачить обмежений діапазон (інакше -Werror=format-truncation) */
        snprintf(dir, sizeof(dir), "%04u%02u%02u", (unsigned)(lt.tm_year + 1900) % 10000u,
                 (unsigned)(lt.tm_mon + 1) % 100u, (unsigned)lt.tm_mday % 100u);
        snprintf(name, sizeof(name), "%02u%02u%02u_%s.jpg", (unsigned)lt.tm_hour % 100u,
                 (unsigned)lt.tm_min % 100u, (unsigned)lt.tm_sec % 100u, tag);
    } else {
        snprintf(dir, sizeof(dir), "nosync");
        snprintf(name, sizeof(name), "up%06u_%s.jpg", (unsigned)(esp_timer_get_time() / 1000000) % 1000000u, tag);
    }

    sd_card_prune_photos(CONFIG_WEATHER_CAM_MIN_FREE_MB);
    bool saved = sd_card_save_photo(dir, name, jpeg, jpeg_len, result->file, sizeof(result->file));
    free(jpeg);
    if (!saved) {
        result->file[0] = '\0';
        fail(result, "write");
        return;
    }
    result->ok = true;
    ESP_LOGI(TAG, "Saved %s (%u bytes)", result->file, (unsigned)jpeg_len);
}

#else  /* камера вимкнена в menuconfig */

bool camera_init(void) { return false; }

void camera_snap(const char *reason, camera_result_t *result)
{
    (void)reason;
    memset(result, 0, sizeof(*result));
    snprintf(result->err, sizeof(result->err), "disabled");
}

#endif
