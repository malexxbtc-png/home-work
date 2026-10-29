#ifndef TRIGGERS_H
#define TRIGGERS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { SNAP_REQ_SENT = 0, SNAP_REQ_BUSY, SNAP_REQ_OFFLINE, SNAP_REQ_COOLDOWN } snap_req_t;

void triggers_init(void);

/* Реєструє подію в журналі. Якщо want_photo — просить ESP32-S3 зробити знімок
   (з урахуванням зв'язку, кулдауну і того, що попередній знімок ще виконується). Повертає seq події. */
uint32_t trigger_event(const char *type, const char *info, bool want_photo);

/* Ручний знімок з меню (кулдаун не застосовується). */
snap_req_t trigger_manual_photo(void);

/* Викликати кожні ~100-200 мс: обробляє snap_done і таймаут очікування. */
void triggers_poll(void);

bool triggers_photo_pending(void);

#endif
