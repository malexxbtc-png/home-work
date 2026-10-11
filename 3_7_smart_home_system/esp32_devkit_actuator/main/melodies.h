#ifndef MELODIES_H
#define MELODIES_H

typedef struct {
    int frequency;
    int duration_ms;
} Note;

typedef struct {
    const char* name;
    const Note* notes;
    int length;
} Melody;

#define PLAYLIST_SIZE 5
extern const Melody custom_playlist[PLAYLIST_SIZE];

#endif // MELODIES_H