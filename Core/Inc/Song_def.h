/*
 * Song_def.h
 *
 *  Created on: Jun 13, 2026
 *      Author: User
 */

#ifndef SONG_DEF_H_
#define SONG_DEF_H_

#include <stdint.h>

// Define the precise structure requested by the project guidelines
typedef struct {
    const char* name1;
    const char* name2;
    const uint16_t* note;  // Frequencies in Hz
    const float* beat;     // Fractional note durations
    uint16_t tempo;        // Beats Per Minute (BPM)
    uint16_t length;       // Total number of notes
} Song;

// Musical pitches represented as raw frequencies
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define REST     0

// Song 0: Twinkle Twinkle
static const uint16_t s0_n[] = {NOTE_C4, NOTE_C4, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_A4, NOTE_G4, REST};
static const float    s0_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 1.0};
static const Song song0 = {"Twinkle", "Little Star", s0_n, s0_b, 120, 8};

// Song 1: Mary Had a Little Lamb
static const uint16_t s1_n[] = {NOTE_E4, NOTE_D4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_E4, REST};
static const float    s1_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 1.0};
static const Song song1 = {"Mary Had", "A Lamb", s1_n, s1_b, 120, 8};

// Song 2: Arpeggio
static const uint16_t s2_n[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_G4, NOTE_E4, NOTE_C4, REST};
static const float    s2_b[] = {0.5, 0.5, 0.5, 1.0, 0.5, 0.5, 1.0, 1.0};
static const Song song2 = {"Arpeggio", "Ascending", s2_n, s2_b, 140, 8};

// Song 3: Descending Scale
static const uint16_t s3_n[] = {NOTE_C5, NOTE_B4, NOTE_A4, NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4, NOTE_C4};
static const float    s3_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
static const Song song3 = {"Scale", "Descending", s3_n, s3_b, 120, 8};

// Song 4: Ode To Joy
static const uint16_t s4_n[] = {NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4};
static const float    s4_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
static const Song song4 = {"Ode to", "Joy Theme", s4_n, s4_b, 120, 8};

// Song 5: Happy Birthday
static const uint16_t s5_n[] = {NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, NOTE_C5, NOTE_B4, REST, REST};
static const float    s5_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 1.0, 1.0};
static const Song song5 = {"Happy", "Birthday", s5_n, s5_b, 120, 8};

// Song 6: Simple Melody
static const uint16_t s6_n[] = {NOTE_C4, NOTE_G4, NOTE_C4, REST, NOTE_G4, NOTE_C4, REST, REST};
static const float    s6_b[] = {1.0, 1.0, 2.0, 1.0, 1.0, 2.0, 1.0, 1.0};
static const Song song6 = {"Simple", "Melody", s6_n, s6_b, 100, 8};

// Song 7: Frere Jacques
static const uint16_t s7_n[] = {NOTE_C4, NOTE_D4, NOTE_E4, NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_C4};
static const float    s7_b[] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
static const Song song7 = {"Frere", "Jacques", s7_n, s7_b, 120, 8};

#endif /* SONG_DEF_H_ */
