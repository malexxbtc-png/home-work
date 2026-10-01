#ifndef MELODIES_H
#define MELODIES_H

#include <stdint.h>

typedef struct {
    int frequency;
    int duration_ms;
} Note;

typedef struct {
    const char* name;
    const Note* notes;
    int length;
} Melody;

extern const Melody classic_playlist[4];

#endif // MELODIES_H