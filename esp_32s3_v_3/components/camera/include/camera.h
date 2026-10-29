#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool ok;
    char file[48];      /* шлях відносно /sdcard/photos, напр. 20261025/194512_pir.jpg */
    char err[24];       /* no_camera | no_sd | capture | encode | write | disabled */
} camera_result_t;

/* Ініціалізує OV7670 (esp32-camera). Безпечно викликати повторно. */
bool camera_init(void);
bool camera_is_ready(void);

/* Робить кадр, кодує в JPEG і зберігає на SD як /sdcard/photos/YYYYMMDD/HHMMSS_<reason>.jpg.
   Блокує на 0.3-2 с. Викликати з однієї задачі. */
void camera_snap(const char *reason, camera_result_t *result);

#endif
