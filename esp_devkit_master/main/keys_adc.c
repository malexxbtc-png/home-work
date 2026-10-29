#include "keys_adc.h"

#include <string.h>
#include "config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "keys_adc";

#define NVS_NS  "keys"
#define NVS_KEY "cal"

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static adc_channel_t channel;
static bool ready;
static key_state_t state;
static int last_mv = -1;

static bool create_cali(adc_unit_t unit, adc_atten_t atten)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = unit, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_curve_fitting(&cfg, &cali) == ESP_OK;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id = unit, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_line_fitting(&cfg, &cali) == ESP_OK;
#else
    (void)unit; (void)atten;
    return false;
#endif
}

static void load_cal(key_cal_t *cal)
{
    keys_default_cal(cal);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    key_cal_t stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(h, NVS_KEY, &stored, &size) == ESP_OK && size == sizeof(stored) &&
        keys_cal_valid(&stored)) {
        *cal = stored;
        ESP_LOGI(TAG, "Loaded key calibration from NVS");
    }
    nvs_close(h);
}

esp_err_t keys_adc_init(void)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(PIN_KEYS_ADC, &unit, &channel);
    if (err != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC1 pin", PIN_KEYS_ADC);
        return ESP_ERR_INVALID_ARG;
    }
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = unit };
    err = adc_oneshot_new_unit(&unit_cfg, &adc);
    if (err != ESP_OK) {
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    err = adc_oneshot_config_channel(adc, channel, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }
    if (!create_cali(unit, ADC_ATTEN_DB_12)) {
        ESP_LOGW(TAG, "No ADC calibration scheme, using a linear approximation");
    }

    key_cal_t cal;
    load_cal(&cal);
    keys_state_init(&state, &cal);
    ready = true;
    return ESP_OK;
}

int keys_adc_read_mv(void)
{
    if (!ready) {
        return -1;
    }
    int64_t sum = 0;
    int n = 0;
    for (int i = 0; i < KEY_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc, channel, &raw) != ESP_OK) {
            continue;
        }
        int mv;
        if (cali == NULL || adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) {
            mv = raw * 3100 / 4095;                /* груба оцінка для 12 дБ */
        }
        sum += mv;
        n++;
    }
    return n ? (int)(sum / n) : -1;
}

key_event_t keys_adc_poll(uint32_t now_ms)
{
    key_event_t none = { KEY_NONE, KEY_EV_NONE };
    int mv = keys_adc_read_mv();
    if (mv < 0) {
        return none;
    }
    last_mv = mv;
    return keys_feed(&state, mv, now_ms);
}

key_id_t keys_adc_current(void) { return state.active; }
int keys_adc_last_mv(void) { return last_mv; }
const key_cal_t *keys_adc_cal(void) { return &state.cal; }

esp_err_t keys_adc_set_cal(const key_cal_t *cal, bool save)
{
    if (!keys_cal_valid(cal)) {
        return ESP_ERR_INVALID_ARG;
    }
    keys_state_init(&state, cal);
    if (!save) {
        return ESP_OK;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, cal, sizeof(*cal));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
