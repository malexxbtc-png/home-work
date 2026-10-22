#include "ssd1306.h"

#include <stdbool.h>
#include <string.h>
#include "esp_log.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_TIMEOUT_MS 100

struct ssd1306_device {
    i2c_master_dev_handle_t i2c;
    uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];
};

static const char *TAG = "ssd1306";

static const uint8_t *glyph_for(char c)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
#define FONT_GLYPH(character, a, b, c_, d, e) \
    case character: { static const uint8_t glyph[5] = {a, b, c_, d, e}; return glyph; }
    switch (c) {
        FONT_GLYPH(' ', 0x00, 0x00, 0x00, 0x00, 0x00)
        FONT_GLYPH('!', 0x00, 0x00, 0x5F, 0x00, 0x00)
        FONT_GLYPH(',', 0x00, 0x50, 0x30, 0x00, 0x00)
        FONT_GLYPH('%', 0x23, 0x13, 0x08, 0x64, 0x62)
        FONT_GLYPH('-', 0x08, 0x08, 0x08, 0x08, 0x08)
        FONT_GLYPH('.', 0x00, 0x60, 0x60, 0x00, 0x00)
        FONT_GLYPH(':', 0x00, 0x36, 0x36, 0x00, 0x00)
        FONT_GLYPH('0', 0x3E, 0x51, 0x49, 0x45, 0x3E)
        FONT_GLYPH('1', 0x00, 0x42, 0x7F, 0x40, 0x00)
        FONT_GLYPH('2', 0x42, 0x61, 0x51, 0x49, 0x46)
        FONT_GLYPH('3', 0x21, 0x41, 0x45, 0x4B, 0x31)
        FONT_GLYPH('4', 0x18, 0x14, 0x12, 0x7F, 0x10)
        FONT_GLYPH('5', 0x27, 0x45, 0x45, 0x45, 0x39)
        FONT_GLYPH('6', 0x3C, 0x4A, 0x49, 0x49, 0x30)
        FONT_GLYPH('7', 0x01, 0x71, 0x09, 0x05, 0x03)
        FONT_GLYPH('8', 0x36, 0x49, 0x49, 0x49, 0x36)
        FONT_GLYPH('9', 0x06, 0x49, 0x49, 0x29, 0x1E)
        FONT_GLYPH('A', 0x7E, 0x11, 0x11, 0x11, 0x7E)
        FONT_GLYPH('B', 0x7F, 0x49, 0x49, 0x49, 0x36)
        FONT_GLYPH('C', 0x3E, 0x41, 0x41, 0x41, 0x22)
        FONT_GLYPH('D', 0x7F, 0x41, 0x41, 0x22, 0x1C)
        FONT_GLYPH('E', 0x7F, 0x49, 0x49, 0x49, 0x41)
        FONT_GLYPH('F', 0x7F, 0x09, 0x09, 0x09, 0x01)
        FONT_GLYPH('G', 0x3E, 0x41, 0x49, 0x49, 0x7A)
        FONT_GLYPH('H', 0x7F, 0x08, 0x08, 0x08, 0x7F)
        FONT_GLYPH('I', 0x00, 0x41, 0x7F, 0x41, 0x00)
        FONT_GLYPH('K', 0x7F, 0x08, 0x14, 0x22, 0x41)
        FONT_GLYPH('L', 0x7F, 0x40, 0x40, 0x40, 0x40)
        FONT_GLYPH('M', 0x7F, 0x02, 0x0C, 0x02, 0x7F)
        FONT_GLYPH('N', 0x7F, 0x04, 0x08, 0x10, 0x7F)
        FONT_GLYPH('O', 0x3E, 0x41, 0x41, 0x41, 0x3E)
        FONT_GLYPH('P', 0x7F, 0x09, 0x09, 0x09, 0x06)
        FONT_GLYPH('R', 0x7F, 0x09, 0x19, 0x29, 0x46)
        FONT_GLYPH('S', 0x46, 0x49, 0x49, 0x49, 0x31)
        FONT_GLYPH('T', 0x01, 0x01, 0x7F, 0x01, 0x01)
        FONT_GLYPH('U', 0x3F, 0x40, 0x40, 0x40, 0x3F)
        FONT_GLYPH('W', 0x3F, 0x40, 0x38, 0x40, 0x3F)
        FONT_GLYPH('a', 0x20, 0x54, 0x54, 0x54, 0x78)
        FONT_GLYPH('c', 0x38, 0x44, 0x44, 0x44, 0x20)
        FONT_GLYPH('d', 0x38, 0x44, 0x44, 0x48, 0x7F)
        FONT_GLYPH('e', 0x38, 0x54, 0x54, 0x54, 0x18)
        FONT_GLYPH('f', 0x08, 0x7E, 0x09, 0x01, 0x02)
        FONT_GLYPH('g', 0x0C, 0x52, 0x52, 0x52, 0x3E)
        FONT_GLYPH('h', 0x7F, 0x08, 0x04, 0x04, 0x78)
        FONT_GLYPH('i', 0x00, 0x44, 0x7D, 0x40, 0x00)
        FONT_GLYPH('k', 0x7F, 0x10, 0x28, 0x44, 0x00)
        FONT_GLYPH('l', 0x00, 0x41, 0x7F, 0x40, 0x00)
        FONT_GLYPH('n', 0x7C, 0x08, 0x04, 0x04, 0x78)
        FONT_GLYPH('o', 0x38, 0x44, 0x44, 0x44, 0x38)
        FONT_GLYPH('p', 0x7C, 0x14, 0x14, 0x14, 0x08)
        FONT_GLYPH('r', 0x7C, 0x08, 0x04, 0x04, 0x08)
        FONT_GLYPH('s', 0x48, 0x54, 0x54, 0x54, 0x20)
        FONT_GLYPH('t', 0x04, 0x3F, 0x44, 0x40, 0x20)
        FONT_GLYPH('u', 0x3C, 0x40, 0x40, 0x20, 0x7C)
        FONT_GLYPH('w', 0x3C, 0x40, 0x30, 0x40, 0x3C)
        default:
            return c == ' ' ? blank : question;
    }
#undef FONT_GLYPH
}

static esp_err_t send_commands(ssd1306_handle_t handle, const uint8_t *commands, size_t count)
{
    uint8_t buffer[32];
    if (handle == NULL || count + 1 > sizeof(buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    buffer[0] = 0x00;
    memcpy(&buffer[1], commands, count);
    return i2c_master_transmit(handle->i2c, buffer, count + 1, OLED_TIMEOUT_MS);
}

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus, uint16_t address, ssd1306_handle_t *handle)
{
    if (bus == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    static struct ssd1306_device device;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &device.i2c);
    if (err != ESP_OK) {
        return err;
    }

    *handle = &device;
    const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    err = send_commands(*handle, init_commands, sizeof(init_commands));
    if (err == ESP_OK) {
        ssd1306_clear(*handle, false);
        ssd1306_show(*handle);
    }
    return err;
}

void ssd1306_clear(ssd1306_handle_t handle, bool color)
{
    if (handle != NULL) {
        memset(handle->framebuffer, color ? 0xFF : 0x00, sizeof(handle->framebuffer));
    }
}

static void draw_pixel(ssd1306_handle_t handle, int x, int y)
{
    if (x >= 0 && x < OLED_WIDTH && y >= 0 && y < OLED_HEIGHT) {
        handle->framebuffer[(y / 8) * OLED_WIDTH + x] |= (uint8_t)(1U << (y % 8));
    }
}

void ssd1306_draw_string(ssd1306_handle_t handle, int x, int y, const char *text)
{
    if (handle == NULL || text == NULL) {
        return;
    }
    for (; *text != '\0'; ++text, x += 6) {
        const uint8_t *glyph = glyph_for(*text);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[column] & (1U << row)) != 0) {
                    draw_pixel(handle, x + column, y + row);
                }
            }
        }
    }
}

/* Збільшений текст: кожен піксель шрифту малюється квадратом scale x scale */
void ssd1306_draw_string_scaled(ssd1306_handle_t handle, int x, int y, const char *text, int scale)
{
    if (handle == NULL || text == NULL || scale < 1) {
        return;
    }
    for (; *text != '\0'; ++text, x += 6 * scale) {
        const uint8_t *glyph = glyph_for(*text);
        for (int column = 0; column < 5; ++column) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[column] & (1U << row)) == 0) {
                    continue;
                }
                for (int dx = 0; dx < scale; ++dx) {
                    for (int dy = 0; dy < scale; ++dy) {
                        draw_pixel(handle, x + column * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

void ssd1306_show(ssd1306_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    const uint8_t window[] = {0x21, 0, OLED_WIDTH - 1, 0x22, 0, 7};
    esp_err_t err = send_commands(handle, window, sizeof(window));
    uint8_t data[OLED_WIDTH + 1];
    data[0] = 0x40;
    for (int page = 0; err == ESP_OK && page < OLED_HEIGHT / 8; ++page) {
        memcpy(&data[1], &handle->framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        err = i2c_master_transmit(handle->i2c, data, sizeof(data), OLED_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display update failed: %s", esp_err_to_name(err));
    }
}