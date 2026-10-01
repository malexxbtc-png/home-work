#include "melodies.h"
#include "notes.h"

static const Note ode_to_joy_notes[] = {
    {NOTE_E6, 250}, {NOTE_E6, 250}, {NOTE_F6, 250}, {NOTE_G6, 250},
    {NOTE_G6, 250}, {NOTE_F6, 250}, {NOTE_E6, 250}, {NOTE_D6, 250},
    {NOTE_C6, 250}, {NOTE_C6, 250}, {NOTE_D6, 250}, {NOTE_E6, 250},
    {NOTE_E6, 350}, {NOTE_D6, 150}, {NOTE_D6, 500}
};

static const Note mozart_notes[] = {
    {NOTE_G6, 300}, {NOTE_REST, 100}, {NOTE_G6, 300}, {NOTE_REST, 100},
    {NOTE_G6, 150}, {NOTE_D6, 150}, {NOTE_G6, 150}, {NOTE_B6, 150},
    {NOTE_D7, 500}
};

static const Note brahms_notes[] = {
    {NOTE_E6, 250}, {NOTE_E6, 250}, {NOTE_G6, 500},
    {NOTE_E6, 250}, {NOTE_E6, 250}, {NOTE_G6, 500},
    {NOTE_E6, 250}, {NOTE_G6, 250}, {NOTE_C7, 400}, {NOTE_B6, 150},
    {NOTE_A6, 250}, {NOTE_A6, 250}, {NOTE_G6, 500}
};

static const Note vivaldi_notes[] = {
    {NOTE_C6, 200}, {NOTE_E6, 200}, {NOTE_E6, 200}, {NOTE_E6, 200},
    {NOTE_D6, 100}, {NOTE_C6, 100}, {NOTE_G6, 400},
    {NOTE_E6, 200}, {NOTE_E6, 200}, {NOTE_D6, 100}, {NOTE_C6, 100},
    {NOTE_G6, 400}
};

const Melody classic_playlist[4] = {
    {"Beethoven - Ode to Joy", ode_to_joy_notes, sizeof(ode_to_joy_notes)/sizeof(ode_to_joy_notes[0])},
    {"Mozart - Eine kleine Nachtmusik", mozart_notes, sizeof(mozart_notes)/sizeof(mozart_notes[0])},
    {"Brahms - Lullaby", brahms_notes, sizeof(brahms_notes)/sizeof(brahms_notes[0])},
    {"Vivaldi - Spring", vivaldi_notes, sizeof(vivaldi_notes)/sizeof(vivaldi_notes[0])}
};