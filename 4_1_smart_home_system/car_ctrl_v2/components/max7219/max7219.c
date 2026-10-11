#include "max7219.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "max7219"

#define REG_DIGIT0    0x01
#define REG_DECODE    0x09
#define REG_INTENSITY 0x0A
#define REG_SCANLIMIT 0x0B
#define REG_SHUTDOWN  0x0C
#define REG_TEST      0x0F

static spi_device_handle_t s_dev;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(s_dev, &t);
}

esp_err_t max7219_init(const max7219_config_t *cfg)
{
    if (!cfg || cfg->intensity > 15) return ESP_ERR_INVALID_ARG;

    const spi_bus_config_t bus = {
        .mosi_io_num     = cfg->mosi_gpio,
        .miso_io_num     = -1,
        .sclk_io_num     = cfg->clk_gpio,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED), TAG, "bus");

    const spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000000,
        .mode           = 0,
        .spics_io_num   = cfg->cs_gpio,
        .queue_size     = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &dev, &s_dev), TAG, "device");

    ESP_RETURN_ON_ERROR(reg_write(REG_TEST, 0x00), TAG, "init");
    ESP_RETURN_ON_ERROR(reg_write(REG_SHUTDOWN, 0x01), TAG, "init");
    ESP_RETURN_ON_ERROR(reg_write(REG_SCANLIMIT, 0x07), TAG, "init");
    ESP_RETURN_ON_ERROR(reg_write(REG_DECODE, 0x00), TAG, "init");
    ESP_RETURN_ON_ERROR(reg_write(REG_INTENSITY, cfg->intensity), TAG, "init");

    const uint8_t blank[8] = {0};
    return max7219_draw(blank);
}

esp_err_t max7219_draw(const uint8_t rows[8])
{
    for (int r = 0; r < 8; r++) {
        ESP_RETURN_ON_ERROR(reg_write(REG_DIGIT0 + r, rows[r]), TAG, "draw");
    }
    return ESP_OK;
}
