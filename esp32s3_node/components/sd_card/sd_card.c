#include "sd_card.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card";

#define MOUNT_POINT     "/sdcard"
#define LOG_FILE        MOUNT_POINT "/weather_log.csv"
#define PHOTO_ROOT      MOUNT_POINT "/photos"
#define VALID_EPOCH_MIN 1704067200LL

static bool sd_initialized;
static int64_t last_attempt_us;
static SemaphoreHandle_t sd_mutex;
static sdmmc_card_t *g_card;

#if CONFIG_WEATHER_SD_DEBUG
/* Діагностика: раз на 500 мс читає сектор 0 і друкує момент, коли карта перестає відповідати.
   За цим часом видно, що саме відбувається в системі (старт Wi-Fi, дисплея, камери). */
static void sd_debug_task(void *arg)
{
  (void)arg;
  uint8_t *buf = heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  int last = -1;
  int64_t last_print_us = 0;
  while (buf != NULL) {
    int ok = 0;
    if (g_card != NULL && xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      ok = sdmmc_read_sectors(g_card, buf, 0, 1) == ESP_OK;
      xSemaphoreGive(sd_mutex);
    }
    int64_t now = esp_timer_get_time();
    if (ok != last || now - last_print_us > 5000000) {
      ESP_LOGW(TAG, "[sd-debug] t=%lld ms: sector 0 read %s", (long long)(now / 1000), ok ? "OK" : "FAILED");
      last = ok;
      last_print_us = now;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  vTaskDelete(NULL);
}
#endif

#if CONFIG_WEATHER_SD_SHARED_BUS
#define SD_HOST SPI2_HOST          /* шина дисплея: її ініціалізує display_init() з MISO для SD */
#else
#define SD_HOST SPI3_HOST          /* окрема шина: дисплей сидить на SPI2 */
#endif

static bool bus_ready;

static bool init_sd_bus(void)
{
  if (bus_ready) {
    return true;
  }
#if CONFIG_WEATHER_SD_SHARED_BUS
  bus_ready = true;                  /* шина вже створена дисплеєм */
  return true;
#else
  const spi_bus_config_t bus = {
      .mosi_io_num = CONFIG_WEATHER_SD_MOSI_PIN,
      .miso_io_num = CONFIG_WEATHER_SD_MISO_PIN,
      .sclk_io_num = CONFIG_WEATHER_SD_SCK_PIN,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };
  esp_err_t err = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "SD SPI bus init failed: %s", esp_err_to_name(err));
    return false;
  }
  bus_ready = true;
  return true;
#endif
}

static bool mount_card(void)
{
  if (!init_sd_bus()) {
    return false;
  }
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 4,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_card_t *card = NULL;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SD_HOST;
  host.max_freq_khz = CONFIG_WEATHER_SD_MAX_FREQ_KHZ;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = CONFIG_WEATHER_SD_CS_PIN;
  slot_config.host_id = host.slot;

  static bool pins_logged;
  if (!pins_logged) {
    pins_logged = true;
#if CONFIG_WEATHER_SD_SHARED_BUS
    ESP_LOGI(TAG, "SD on the display bus SPI2 (SCK/MOSI as TFT) MISO=%d CS=%d, %d kHz",
             CONFIG_WEATHER_SD_MISO_PIN, CONFIG_WEATHER_SD_CS_PIN, CONFIG_WEATHER_SD_MAX_FREQ_KHZ);
#else
    ESP_LOGI(TAG, "SD on own SPI3: SCK=%d MOSI=%d MISO=%d CS=%d, %d kHz", CONFIG_WEATHER_SD_SCK_PIN,
             CONFIG_WEATHER_SD_MOSI_PIN, CONFIG_WEATHER_SD_MISO_PIN, CONFIG_WEATHER_SD_CS_PIN,
             CONFIG_WEATHER_SD_MAX_FREQ_KHZ);
#endif
  }
  esp_err_t ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to mount SD card (%s). Logging disabled.", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "SD card mounted at " MOUNT_POINT);
  g_card = card;
#if CONFIG_WEATHER_SD_DEBUG
  static bool debug_started;
  if (!debug_started) {
    debug_started = xTaskCreate(sd_debug_task, "sd_debug", 4096, NULL, 2, NULL) == pdPASS;
  }
#endif
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
  /* Карті після подачі живлення потрібен час, а проводи SPI можуть давати плаваючі збої:
     пробуємо до трьох разів із паузою */
  for (int attempt = 0; attempt < 3 && !sd_initialized; attempt++) {
    if (attempt > 0) {
      vTaskDelay(pdMS_TO_TICKS(300));
    }
    sd_initialized = mount_card();
  }
  last_attempt_us = esp_timer_get_time();
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

/* Для CSV дозволяємо ще '/' і '.' (шлях знімка) */
static void sanitize_path(const char *in, char *out, size_t out_size)
{
  size_t n = 0;
  for (; in != NULL && *in != '\0' && n + 1 < out_size; in++) {
    char c = *in;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '/' || c == '.';
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
                       float t_out, float wind, const char *photo)
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
      fprintf(f, "time,reason,t_local,rh_local,p_local_hpa,t_out,wind_ms,photo\n");
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
    char safe_photo[48];
    sanitize_path(photo, safe_photo, sizeof(safe_photo));
    fprintf(f, ",%s\n", safe_photo);
    ok = fclose(f) == 0;
  }
  xSemaphoreGive(sd_mutex);
  return ok;
}

/* ---------- знімки ---------- */

static void make_dir(const char *path)
{
  if (mkdir(path, 0775) != 0 && errno != EEXIST) {
    ESP_LOGW(TAG, "mkdir %s failed: errno %d", path, errno);
  }
}

bool sd_card_save_photo(const char *dir, const char *name, const void *data, size_t len,
                        char *rel_path, size_t rel_size)
{
  if (!sd_initialized || sd_mutex == NULL || dir == NULL || name == NULL || data == NULL || len == 0) {
    return false;
  }
  if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return false;
  }

  make_dir(PHOTO_ROOT);
  char dir_path[48];
  snprintf(dir_path, sizeof(dir_path), PHOTO_ROOT "/%s", dir);
  make_dir(dir_path);

  /* Не перезаписуємо: за збігу імені додаємо _1, _2 ... перед розширенням */
  char base[40];
  snprintf(base, sizeof(base), "%s", name);
  char *dot = strrchr(base, '.');
  char ext[8] = "";
  if (dot != NULL) {
    snprintf(ext, sizeof(ext), "%s", dot);
    *dot = '\0';
  }
  char file_name[64];
  char path[160];
  struct stat st;
  int attempt = 0;
  do {
    if (attempt == 0) snprintf(file_name, sizeof(file_name), "%s%s", base, ext);
    else snprintf(file_name, sizeof(file_name), "%s_%d%s", base, attempt, ext);
    snprintf(path, sizeof(path), "%s/%s", dir_path, file_name);
    attempt++;
  } while (stat(path, &st) == 0 && attempt < 20);

  bool ok = false;
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Cannot create %s (errno %d)", path, errno);
  } else {
    size_t written = fwrite(data, 1, len, f);
    bool closed = fclose(f) == 0;
    ok = written == len && closed;
    if (!ok) {
      ESP_LOGE(TAG, "Short write to %s (%u of %u)", path, (unsigned)written, (unsigned)len);
      unlink(path);
    }
  }
  if (ok && rel_path != NULL) {
    snprintf(rel_path, rel_size, "%s/%s", dir, file_name);
  }
  xSemaphoreGive(sd_mutex);
  return ok;
}

static int compare_names(const void *a, const void *b)
{
  return strcmp((const char *)a, (const char *)b);
}

/* Видаляє вміст і саму папку дня */
static void remove_day_dir(const char *name)
{
  char dir_path[48];
  snprintf(dir_path, sizeof(dir_path), PHOTO_ROOT "/%s", name);
  DIR *d = opendir(dir_path);
  if (d != NULL) {
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
      char file_path[320];
      snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
      unlink(file_path);
    }
    closedir(d);
  }
  rmdir(dir_path);
  ESP_LOGW(TAG, "Removed old photo folder %s to free space", name);
}

void sd_card_prune_photos(uint32_t min_free_mb)
{
  if (!sd_initialized || sd_mutex == NULL) {
    return;
  }
  if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return;
  }
  for (int round = 0; round < 366; round++) {
    uint64_t total = 0, free_bytes = 0;
    if (esp_vfs_fat_info(MOUNT_POINT, &total, &free_bytes) != ESP_OK ||
        free_bytes >= (uint64_t)min_free_mb * 1024 * 1024) {
      break;
    }
    /* Найстаріша папка YYYYMMDD (імена сортуються за алфавітом = за датою) */
    char oldest[16] = "";
    char newest[16] = "";
    DIR *d = opendir(PHOTO_ROOT);
    if (d == NULL) {
      break;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
      if (entry->d_name[0] == '.' || strlen(entry->d_name) >= sizeof(oldest)) {
        continue;
      }
      if (oldest[0] == '\0' || compare_names(entry->d_name, oldest) < 0) {
        memcpy(oldest, entry->d_name, strlen(entry->d_name) + 1);   /* довжину вже перевірено вище */
      }
      if (compare_names(entry->d_name, newest) > 0) {
        memcpy(newest, entry->d_name, strlen(entry->d_name) + 1);
      }
    }
    closedir(d);
    if (oldest[0] == '\0' || strcmp(oldest, newest) == 0) {
      break;                                   /* лишилася тільки остання папка: не чіпаємо */
    }
    remove_day_dir(oldest);
  }
  xSemaphoreGive(sd_mutex);
}
