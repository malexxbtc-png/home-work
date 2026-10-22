#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>

/* Монтує SD (SPI2). Викликати після display_init(), яка ініціалізує шину. */
bool sd_card_init(void);

bool sd_card_is_ready(void);

/* Додає рядок у /sdcard/weather_log.csv.
   NAN у полі записується порожнім значенням. */
bool sd_card_log_event(const char *reason, float t_local, float rh_local, float p_local,
                       float t_out, float wind);

#endif /* SD_CARD_H */
