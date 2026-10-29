#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Монтує SD (SPI2). Викликати після display_init(), яка ініціалізує шину. */
bool sd_card_init(void);

bool sd_card_is_ready(void);

/* Додає рядок у /sdcard/weather_log.csv.
   NAN у полі записується порожнім значенням. */
bool sd_card_log_event(const char *reason, float t_local, float rh_local, float p_local,
                       float t_out, float wind, const char *photo);

/* Зберігає JPEG як /sdcard/photos/<dir>/<name> (папку створює; за збігу імені додає _1, _2...).
   rel_path (може бути NULL) отримує "<dir>/<фактичне ім'я>". */
bool sd_card_save_photo(const char *dir, const char *name, const void *data, size_t len,
                        char *rel_path, size_t rel_size);

/* Якщо вільного місця менше за min_free_mb, видаляє найстаріші папки днів у /sdcard/photos
   (останню папку не чіпає). */
void sd_card_prune_photos(uint32_t min_free_mb);

#endif /* SD_CARD_H */
