#include "pin_check.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define PIN_LOGE(...) ESP_LOGE("pins", __VA_ARGS__)
#else
#include <stdio.h>
#define PIN_LOGE(...) do { fprintf(stderr, "E pins: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#endif

#define GPIO_MAX 48

typedef struct {
    int         from;
    int         to;
    const char *why;
} reserved_t;

/* Для ESP32-S3 (за документацією Espressif) */
static const reserved_t RESERVED[] = {
    { 0,  0, "strapping: режим завантаження"},
    { 3,  3, "strapping: джерело JTAG"},
    {45, 46, "strapping: напруга SPI і лог ROM"},
    {19, 20, "USB D-/D+"},
    {26, 32, "внутрішня SPI flash/PSRAM"},
    {33, 37, "PSRAM (octal) або flash на деяких модулях"},
    {39, 42, "JTAG"},
    {43, 44, "консоль UART0 (логи)"},
};

int pin_check(const int *pins, const char *const *names, int n)
{
    int errors = 0;

    for (int i = 0; i < n; i++) {
        const int p = pins[i];
        if (p < 0) {
            continue;                       /* не використовується */
        }
        if (p > GPIO_MAX) {
            PIN_LOGE("%s=%d: такого GPIO немає", names[i], p);
            errors++;
            continue;
        }
        for (unsigned r = 0; r < sizeof(RESERVED) / sizeof(RESERVED[0]); r++) {
            if (p >= RESERVED[r].from && p <= RESERVED[r].to) {
                PIN_LOGE("%s=GPIO%d: зарезервовано (%s)", names[i], p, RESERVED[r].why);
                errors++;
                break;
            }
        }
        for (int j = 0; j < i; j++) {
            if (pins[j] == p) {
                PIN_LOGE("GPIO%d призначено двом сигналам: %s і %s", p, names[j], names[i]);
                errors++;
                break;
            }
        }
    }
    return errors;
}
