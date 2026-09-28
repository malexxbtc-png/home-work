#include "sound_dac.h"
#include "driver/dac_cosine.h"
#include "esp_log.h"

static const char *TAG = "SOUND_DAC";
static dac_cosine_handle_t dac_handle = NULL;
static gpio_num_t dac_gpio = GPIO_NUM_25;

void sound_dac_init(gpio_num_t gpio_num) {
    dac_gpio = gpio_num;
    ESP_LOGI(TAG, "Ініціалізація DAC на GPIO %d", gpio_num);
}

void sound_dac_set_tone(uint16_t freq) {
    if (dac_handle != NULL) {
        dac_cosine_stop(dac_handle);     
        dac_cosine_del_channel(dac_handle); 
        dac_handle = NULL;
    }

    if (freq == 0) return;

   
    dac_cosine_config_t dac_cfg = {
        .chan_id = (dac_gpio == GPIO_NUM_26) ? DAC_CHAN_1 : DAC_CHAN_0,
        .freq_hz = freq,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten = DAC_COSINE_ATTEN_DB_0,
        .phase = DAC_COSINE_PHASE_0,
    };
    
    if (dac_cosine_new_channel(&dac_cfg, &dac_handle) == ESP_OK) {
        dac_cosine_start(dac_handle);
    } else {
        ESP_LOGE(TAG, "Помилка створення DAC каналу для частоти %d Гц", freq);
    }
}

void sound_dac_stop(void) {
    sound_dac_set_tone(0);
}