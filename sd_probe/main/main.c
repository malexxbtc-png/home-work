/* Пробник SD-картки по SPI: без файлової системи, без камери і дисплея.
   Друкує рівні на лініях і сирі байти відповіді картки на CMD0, CMD8, ACMD41, CMD58.
   Піни змінюйте нижче. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_SCK   19
#define PIN_MOSI  20
#define PIN_MISO  40
#define PIN_CS    21
#define HOST      SPI3_HOST
#define CLK_HZ    400000

static spi_device_handle_t dev;

static void xfer(const uint8_t *tx, uint8_t *rx, size_t n)
{
    spi_transaction_t t = {.length = n * 8, .tx_buffer = tx, .rx_buffer = rx};
    if (spi_device_polling_transmit(dev, &t) != ESP_OK) {
        printf("  SPI transfer error\n");
    }
}

static uint8_t byte_xfer(uint8_t out)
{
    uint8_t in = 0xFF;
    xfer(&out, &in, 1);
    return in;
}

/* Читає рівень піна з внутрішньою підтяжкою вгору і вниз */
static void level_test(const char *name, int pin)
{
    gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE};
    gpio_config(&c);
    vTaskDelay(pdMS_TO_TICKS(5));
    int up = gpio_get_level(pin);
    c.pull_up_en = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&c);
    vTaskDelay(pdMS_TO_TICKS(5));
    int down = gpio_get_level(pin);
    const char *verdict = up && down ? "тягнеться ВГОРУ ззовні (підтяжка модуля/картка: добре для MISO і CS)"
                          : !up && !down ? "тягнеться ВНИЗ або замкнений на GND (погано)"
                          : "плаває (зовнішньої підтяжки немає)";
    printf("%-5s GPIO%-2d: підтяжка вгору=%d, вниз=%d -> %s\n", name, pin, up, down, verdict);
}

/* Надсилає команду, друкує R1 (і extra байтів) */
static uint8_t command(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *extra, int extra_n, bool keep_cs)
{
    gpio_set_level(PIN_CS, 0);
    byte_xfer(0xFF);
    uint8_t frame[6] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                        (uint8_t)(arg >> 8), (uint8_t)arg, crc};
    uint8_t rx[6];
    xfer(frame, rx, 6);
    uint8_t r1 = 0xFF;
    uint8_t seen[8];
    for (int i = 0; i < 8; i++) {
        seen[i] = byte_xfer(0xFF);
        if (seen[i] != 0xFF && r1 == 0xFF) {
            r1 = seen[i];
            for (int k = 0; k < extra_n; k++) {
                extra[k] = byte_xfer(0xFF);
            }
            break;
        }
    }
    printf("  CMD%-2u -> R1=0x%02X", cmd, r1);
    for (int k = 0; r1 != 0xFF && k < extra_n; k++) {
        printf(" %02X", extra[k]);
    }
    printf("%s\n", r1 == 0xFF ? "  (немає відповіді)" : "");
    if (!keep_cs) {
        gpio_set_level(PIN_CS, 1);
        byte_xfer(0xFF);
    }
    return r1;
}

static void card_test(void)
{
    uint8_t ext[4] = {0};
    gpio_set_level(PIN_CS, 1);
    for (int i = 0; i < 10; i++) {
        byte_xfer(0xFF);                                  /* 80 тактів при CS=1 */
    }
    uint8_t r = command(0, 0, 0x95, NULL, 0, false);
    printf("  очікується 0x01 (card idle)%s\n", r == 0x01 ? " - ОК" : " - ПОМИЛКА");
    r = command(8, 0x1AA, 0x87, ext, 4, false);
    printf("  очікується 0x01 00 00 01 AA%s\n",
           (r == 0x01 && ext[2] == 0x01 && ext[3] == 0xAA) ? " - ОК" : " - ПОМИЛКА");
    int tries = 0;
    for (; tries < 100; tries++) {
        command(55, 0, 0x65, NULL, 0, false);
        r = command(41, 0x40000000, 0x77, NULL, 0, false);
        if (r == 0x00) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("  ACMD41: %s після %d спроб\n", r == 0 ? "готова" : "НЕ готова", tries + 1);
    if (r == 0) {
        command(58, 0, 0xFD, ext, 4, false);
    }
}

void app_main(void)
{
    printf("\n=== SD probe: SCK=%d MOSI=%d MISO=%d CS=%d, %d Hz ===\n", PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS, CLK_HZ);

    level_test("MISO", PIN_MISO);
    level_test("CS", PIN_CS);

    spi_bus_config_t bus = {.mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCK,
                            .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64};
    if (spi_bus_initialize(HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
        printf("spi_bus_initialize FAILED\n");
        return;
    }
    spi_device_interface_config_t cfg = {.clock_speed_hz = CLK_HZ, .mode = 0, .spics_io_num = -1, .queue_size = 1};
    if (spi_bus_add_device(HOST, &cfg, &dev) != ESP_OK) {
        printf("spi_bus_add_device FAILED\n");
        return;
    }
    gpio_config_t cs = {.pin_bit_mask = 1ULL << PIN_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cs);
    gpio_set_level(PIN_CS, 1);

    while (1) {
        /* Петля MOSI-MISO: якщо закоротити ці два дроти (картку витягнути), байти мають повернутись */
        uint8_t tx[4] = {0xA5, 0x5A, 0x3C, 0xC3}, rx[4] = {0};
        xfer(tx, rx, 4);
        printf("\nТест петлі MOSI->MISO: надіслано A5 5A 3C C3, прийнято %02X %02X %02X %02X%s\n", rx[0], rx[1], rx[2], rx[3],
               memcmp(tx, rx, 4) == 0 ? "  <- петля працює (дроти замкнені)" : "");
        printf("Картка:\n");
        card_test();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
