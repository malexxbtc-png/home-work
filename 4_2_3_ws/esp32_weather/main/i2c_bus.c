#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_WS_I2C_SDA,
        .scl_io_num = CONFIG_WS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_bus);
}

esp_err_t i2c_bus_add(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = scl_hz,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, out);
}

void i2c_bus_scan_log(void)
{
    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_bus, a, 20) == ESP_OK) {
            ESP_LOGI(TAG, "device found at 0x%02X", a);
            found++;
        }
    }
    if (!found) {
        ESP_LOGW(TAG, "no I2C devices found: check wiring and pull-ups");
    }
}
