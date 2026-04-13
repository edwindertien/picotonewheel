#pragma once
#include <stdint.h>
#include <math.h>
#include "config.h"

// ============================================================
//  wavetable.h  –  Sine look-up table (LUT)
//
//  One period of a sine wave, stored as 16-bit signed integers
//  in the range –32767 … +32767.
//
//  Generated once at startup rather than stored in flash so
//  we can easily swap in alternative waveforms (e.g. a lightly
//  distorted sine to mimic real tonewheel impurity).
// ============================================================

extern int16_t sineTable[WAVETABLE_SIZE];

inline void wavetable_init()
{
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        // Pure sine – scale to 16-bit signed peak
        sineTable[i] = (int16_t)(32767.0f * sinf(2.0f * M_PI * i / WAVETABLE_SIZE));
    }
}

// Optional: replace the above sine with a Hammond-style slightly
// impure tonewheel waveform.  Real tonewheels are close to a sine
// but carry a tiny 2nd-harmonic component due to the gear tooth
// profile.  Uncomment and call instead of wavetable_init() if
// you want that subtle extra character.
//
// inline void wavetable_init_tonewheel()
// {
//     for (int i = 0; i < WAVETABLE_SIZE; i++) {
//         float theta = 2.0f * M_PI * i / WAVETABLE_SIZE;
//         float v = sinf(theta) + 0.04f * sinf(2.0f * theta);
//         // normalise to ±1 then scale
//         sineTable[i] = (int16_t)(v / 1.04f * 32767.0f);
//     }
// }
