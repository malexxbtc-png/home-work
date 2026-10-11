#include "melodies.h"
#include "notes.h"

// 1. Гімн України ("Ще не вмерла...")
static const Note anthem_ua_notes[] = {
    {NOTE_G4, 400}, {NOTE_G4, 200}, {NOTE_A4, 200}, {NOTE_B4, 400}, 
    {NOTE_G4, 400}, {NOTE_C5, 400}, {NOTE_C5, 400}, {NOTE_C5, 200}, 
    {NOTE_B4, 200}, {NOTE_A4, 200}, {NOTE_B4, 600}, {NOTE_REST, 200}
};

// 2. Ти ж мене підманула (приспів)
static const Note pidmanula_notes[] = {
    {NOTE_E5, 250}, {NOTE_E5, 250}, {NOTE_E5, 250}, {NOTE_C5, 250},
    {NOTE_D5, 250}, {NOTE_D5, 250}, {NOTE_D5, 250}, {NOTE_B4, 250},
    {NOTE_C5, 250}, {NOTE_C5, 250}, {NOTE_A4, 250}, {NOTE_B4, 250},
    {NOTE_B4, 250}, {NOTE_GS4, 250}, {NOTE_A4, 500}, {NOTE_REST, 200}
};

// 3. Eminem - Lose Yourself
static const Note lose_yourself_notes[] = {
    {NOTE_D4, 200}, {NOTE_D4, 200}, {NOTE_D4, 200}, {NOTE_F4, 200},
    {NOTE_D4, 200}, {NOTE_C4, 200}, {NOTE_D4, 400}, {NOTE_REST, 100},
    {NOTE_D4, 200}, {NOTE_D4, 200}, {NOTE_D4, 200}, {NOTE_F4, 200},
    {NOTE_D4, 200}, {NOTE_C4, 200}, {NOTE_D4, 400}
};

// 4. Ennio Morricone - The Good, the Bad and the Ugly
static const Note morricone_notes[] = {
    {NOTE_A4, 150}, {NOTE_D5, 150}, {NOTE_A4, 150}, {NOTE_D5, 150}, 
    {NOTE_A4, 150}, {NOTE_F5, 400}, {NOTE_REST, 150}, 
    {NOTE_E5, 400}, {NOTE_REST, 150}, 
    {NOTE_D5, 600}, {NOTE_REST, 300}
};

// 5. Hans Zimmer - He's a Pirate
static const Note zimmer_notes[] = {
    {NOTE_D4, 150}, {NOTE_D4, 150}, {NOTE_D4, 300}, {NOTE_E4, 150}, 
    {NOTE_F4, 300}, {NOTE_F4, 300}, {NOTE_F4, 150}, {NOTE_G4, 150}, 
    {NOTE_E4, 300}, {NOTE_E4, 300}, {NOTE_D4, 150}, {NOTE_C4, 150}, 
    {NOTE_D4, 600}
};

const Melody custom_playlist[PLAYLIST_SIZE] = {
    {"Ukrainian Anthem", anthem_ua_notes, sizeof(anthem_ua_notes)/sizeof(anthem_ua_notes[0])},
    {"Pidmanula", pidmanula_notes, sizeof(pidmanula_notes)/sizeof(pidmanula_notes[0])},
    {"Eminem - Lose Yourself", lose_yourself_notes, sizeof(lose_yourself_notes)/sizeof(lose_yourself_notes[0])},
    {"Morricone Theme", morricone_notes, sizeof(morricone_notes)/sizeof(morricone_notes[0])},
    {"Zimmer - Pirates", zimmer_notes, sizeof(zimmer_notes)/sizeof(zimmer_notes[0])}
};