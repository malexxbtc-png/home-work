#include "display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "forecast.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link_pico.h"
#include "lvgl.h"

static const char *TAG = "display";

#define LCD_HOST              SPI2_HOST
#define TFT_PHYSICAL_W        76          /* коротка сторона */
#define TFT_PHYSICAL_H        284         /* довга сторона */
#define TFT_DRAW_BUFFER_LINES 10
#define LCD_PIXEL_CLOCK_HZ    (20 * 1000 * 1000)
#define VALID_EPOCH_MIN       1704067200LL

#define PIN_NUM_SCLK    39
#define PIN_NUM_MOSI    38
#define PIN_NUM_MISO    40
#define PIN_NUM_LCD_CS  41
#define PIN_NUM_LCD_DC  42
#define PIN_NUM_LCD_RST 47

/* Розкладка 284x76: три колонки зверху і смуга тривог знизу */
#define COL1_X 2
#define COL1_W 80
#define SEP1_X 84
#define COL2_X 88
#define COL2_W 94
#define SEP2_X 184
#define COL3_X 188
#define COL3_W 94
#define COLS_H 60

static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *label_weekday;
static lv_obj_t *label_local;
static lv_obj_t *label_street;
static lv_obj_t *label_alert;
static lv_disp_drv_t disp_drv;

static void lvgl_tick_cb(void *arg)
{
  (void)arg;
  lv_tick_inc(5);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  (void)panel_io;
  (void)edata;
  lv_disp_flush_ready((lv_disp_drv_t *)user_ctx);
  return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
  esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
  esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1,
                                            area->x2 + 1, area->y2 + 1, color_map);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Draw failed: %s", esp_err_to_name(err));
    lv_disp_flush_ready(drv);     /* без цього LVGL зависає, чекаючи завершення */
  }
}

/* ---------- вміст ---------- */

static void update_clock(void)
{
  time_t now = time(NULL);
  if (now < VALID_EPOCH_MIN) {
    lv_label_set_text(label_time, "--:--");
    lv_label_set_text(label_date, "--.--.----");
    lv_label_set_text(label_weekday, "syncing...");
    return;
  }

  struct tm local_time;
  localtime_r(&now, &local_time);
  char buf[24];
  strftime(buf, sizeof(buf), "%H:%M", &local_time);
  lv_label_set_text(label_time, buf);
  strftime(buf, sizeof(buf), "%d.%m.%Y", &local_time);
  lv_label_set_text(label_date, buf);
  strftime(buf, sizeof(buf), "%A", &local_time);
  lv_label_set_text(label_weekday, buf);
}

static bool json_number(const cJSON *object, const char *key, double *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
    return false;
  }
  *value = item->valuedouble;
  return true;
}

/* Оновлює середню колонку і рядок тривог. Повертає, чи свіжі дані від Pico. */
static void update_local_and_alerts(void)
{
  char pico_buf[CONFIG_WEATHER_LINK_PICO_MAX_JSON_SIZE];
  uint32_t age_ms = 0;
  bool have = link_pico_get_latest_data(pico_buf, sizeof(pico_buf), &age_ms);
  bool fresh = have && age_ms <= CONFIG_WEATHER_LINK_PICO_DATA_STALE_TIMEOUT_MS;

  char local_buf[128] = "LOCAL\nwaiting for\nPico...";
  char alert_buf[96] = "PICO OFFLINE";
  lv_color_t alert_color = lv_color_hex(0xFFB000);

  cJSON *root = have ? cJSON_Parse(pico_buf) : NULL;
  if (root != NULL) {
    double t, rh, p, td;
    char t_s[16] = "--", rh_s[16] = "--", p_s[16] = "--", td_s[16] = "--";
    if (json_number(root, "T", &t))   snprintf(t_s, sizeof(t_s), "%.1fC", t);
    if (json_number(root, "RH", &rh)) snprintf(rh_s, sizeof(rh_s), "%.0f%%", rh);
    if (json_number(root, "P", &p))   snprintf(p_s, sizeof(p_s), "%.0f hPa", p);
    if (json_number(root, "Td", &td)) snprintf(td_s, sizeof(td_s), "%.1fC", td);
    snprintf(local_buf, sizeof(local_buf), "LOCAL%s\nT  %s\nRH %s\nP  %s\nTd %s",
             fresh ? "" : " (old)", t_s, rh_s, p_s, td_s);

    if (fresh) {
      const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alert");
      size_t used = 0;
      alert_buf[0] = '\0';
      if (cJSON_IsArray(alerts)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, alerts) {
          if (cJSON_IsString(item) && item->valuestring != NULL) {
            int n = snprintf(alert_buf + used, sizeof(alert_buf) - used, "%s%s",
                             used ? ", " : "ALERT: ", item->valuestring);
            if (n < 0 || (size_t)n >= sizeof(alert_buf) - used) {
              break;
            }
            used += (size_t)n;
          }
        }
      }
      if (used == 0) {
        snprintf(alert_buf, sizeof(alert_buf), "No alerts");
        alert_color = lv_color_hex(0x30D060);
      } else {
        alert_color = lv_color_hex(0xFF4040);
      }
    }
    cJSON_Delete(root);
  }

  lv_label_set_text(label_local, local_buf);
  lv_label_set_text(label_alert, alert_buf);
  lv_obj_set_style_text_color(label_alert, alert_color, LV_PART_MAIN);
}

static void update_street(void)
{
  current_wx_t wx;
  if (!forecast_get_current(&wx)) {
    lv_label_set_text(label_street, "STREET\nwaiting for\nweather...\n\nopen-meteo.com");
    return;
  }

  char range_s[24] = "";
  if (wx.forecast_days > 0) {
    snprintf(range_s, sizeof(range_s), "Day %.0f..%.0fC",
             wx.daily[0].temperature_min, wx.daily[0].temperature_max);
  }
  char buf[160];
  snprintf(buf, sizeof(buf), "STREET%s\nT %.1fC W %.1f\nRain %.1f S %.1f\n%s\nopen-meteo.com",
           wx.is_stale ? " (old)" : "", wx.temp, wx.wind_speed,
           wx.precipitation, wx.snowfall, range_s);
  lv_label_set_text(label_street, buf);
}

static void update_display_data(void)
{
  update_clock();
  update_local_and_alerts();
  update_street();
}

static void display_task(void *arg)
{
  (void)arg;
  TickType_t last_update = 0;
  bool first = true;

  while (1) {
    TickType_t now = xTaskGetTickCount();
    if (first || (now - last_update) >= pdMS_TO_TICKS(1000)) {
      update_display_data();
      last_update = now;
      first = false;
    }
    lv_timer_handler();               /* LVGL потрібно викликати часто */
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ---------- ініціалізація ---------- */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t x, lv_coord_t y,
                            lv_coord_t width)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
  return label;
}

static void make_separator(lv_obj_t *parent, lv_coord_t x)
{
  lv_obj_t *sep = lv_obj_create(parent);
  lv_obj_set_size(sep, 1, COLS_H);
  lv_obj_align(sep, LV_ALIGN_TOP_LEFT, x, 0);
  lv_obj_set_style_bg_color(sep, lv_color_hex(0x505050), LV_PART_MAIN);
  lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
  lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
}

void display_init(void)
{
  spi_bus_config_t buscfg = {
      .sclk_io_num = PIN_NUM_SCLK,
      .mosi_io_num = PIN_NUM_MOSI,
      .miso_io_num = PIN_NUM_MISO,
      .quadwp_io_num = -1,            /* без цього поле = 0, і SPI займає GPIO0 */
      .quadhd_io_num = -1,
      .max_transfer_sz = TFT_PHYSICAL_H * TFT_DRAW_BUFFER_LINES * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = PIN_NUM_LCD_DC,
      .cs_gpio_num = PIN_NUM_LCD_CS,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = notify_lvgl_flush_ready,
      .user_ctx = &disp_drv,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_NUM_LCD_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
      .bits_per_pixel = 16,
  };
  /* ST7789P3 76x284 сумісний з драйвером ST7789 */
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, CONFIG_WEATHER_DISPLAY_GAP_X,
                                        CONFIG_WEATHER_DISPLAY_GAP_Y));
#if CONFIG_WEATHER_DISPLAY_INVERT_COLORS
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  lv_init();
  const esp_timer_create_args_t tick_timer_args = {.callback = lvgl_tick_cb, .name = "lvgl_tick"};
  esp_timer_handle_t tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 5000));

  static lv_color_t buf1[TFT_PHYSICAL_H * TFT_DRAW_BUFFER_LINES];
  static lv_color_t buf2[TFT_PHYSICAL_H * TFT_DRAW_BUFFER_LINES];
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_PHYSICAL_H * TFT_DRAW_BUFFER_LINES);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = TFT_PHYSICAL_H;       /* 284 */
  disp_drv.ver_res = TFT_PHYSICAL_W;       /* 76 */
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.user_data = panel_handle;
  lv_disp_drv_register(&disp_drv);

  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_white(), LV_PART_MAIN);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  make_separator(screen, SEP1_X);
  make_separator(screen, SEP2_X);

  /* Ліва колонка: час великий, під ним дата і день тижня */
  label_time = make_label(screen, &lv_font_montserrat_28, COL1_X, 2, COL1_W);
  label_date = make_label(screen, &lv_font_montserrat_10, COL1_X, 38, COL1_W);
  label_weekday = make_label(screen, &lv_font_montserrat_10, COL1_X, 50, COL1_W);

  /* Середня і права колонки */
  label_local = make_label(screen, &lv_font_montserrat_10, COL2_X, 2, COL2_W);
  label_street = make_label(screen, &lv_font_montserrat_10, COL3_X, 2, COL3_W);

  /* Нижня смуга: тривоги на всю ширину */
  label_alert = make_label(screen, &lv_font_montserrat_12, COL1_X, COLS_H + 2, 280);
  lv_label_set_text(label_alert, "starting...");
}

void display_start_task(void)
{
  if (xTaskCreate(display_task, "display_task", 8192, NULL, 3, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create display task");
  }
}
