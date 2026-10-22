#include "sd_card.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "sdkconfig.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card";

#define MOUNT_POINT     "/sdcard"
#define LOG_FILE        MOUNT_POINT "/weather_log.csv"
#define VALID_EPOCH_MIN 1704067200LL

static bool sd_initialized;
static int64_t last_attempt_us;
static SemaphoreHandle_t sd_mutex;

static bool mount_card(void)
{
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 4,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_card_t *card = NULL;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;                         /* та сама шина, що й дисплей */
  host.max_freq_khz = CONFIG_WEATHER_SD_MAX_FREQ_KHZ;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = CONFIG_WEATHER_SD_CS_PIN;
  slot_config.host_id = host.slot;

  esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to mount SD card (%s). Logging disabled.", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "SD card mounted at " MOUNT_POINT);
  return true;
}

bool sd_card_init(void)
{
  if (sd_mutex == NULL) {
    sd_mutex = xSemaphoreCreateMutex();
    if (sd_mutex == NULL) {
      return false;
    }
  }
  if (sd_initialized) {
    return true;
  }
  last_attempt_us = esp_timer_get_time();
  sd_initialized = mount_card();
  return sd_initialized;
}

bool sd_card_is_ready(void)
{
  return sd_initialized;
}

/* Залишає в причині лише безпечні для CSV символи */
static void sanitize(const char *in, char *out, size_t out_size)
{
  size_t n = 0;
  for (; in != NULL && *in != '\0' && n + 1 < out_size; in++) {
    char c = *in;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    out[n++] = ok ? c : '_';
  }
  out[n] = '\0';
}

static void put_number(FILE *f, float value)
{
  if (isfinite(value)) {
    fprintf(f, "%.2f", value);
  }
}

bool sd_card_log_event(const char *reason, float t_local, float rh_local, float p_local,
                       float t_out, float wind)
{
  if (sd_mutex == NULL) {
    return false;
  }
  if (!sd_initialized) {
    /* Карту не знайдено: не мучимо шину частими спробами */
    if ((esp_timer_get_time() - last_attempt_us) < (int64_t)CONFIG_WEATHER_SD_RETRY_INTERVAL_S * 1000000 ||
        !sd_card_init()) {
      return false;
    }
  }

  bool ok = false;
  if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return false;
  }

  struct stat st;
  bool new_file = stat(LOG_FILE, &st) != 0;
  FILE *f = fopen(LOG_FILE, "a");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open log file for appending");
  } else {
    if (new_file) {
      fprintf(f, "time,reason,t_local,rh_local,p_local_hpa,t_out,wind_ms\n");
    }

    char time_str[32] = "unsynced";
    time_t now = time(NULL);
    if (now >= VALID_EPOCH_MIN) {
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    char safe_reason[24];
    sanitize(reason, safe_reason, sizeof(safe_reason));

    fprintf(f, "%s,%s,", time_str, safe_reason);
    put_number(f, t_local);  fputc(',', f);
    put_number(f, rh_local); fputc(',', f);
    put_number(f, p_local);  fputc(',', f);
    put_number(f, t_out);    fputc(',', f);
    put_number(f, wind);
    fputc('\n', f);
    ok = fclose(f) == 0;
  }
  xSemaphoreGive(sd_mutex);
  return ok;
}
