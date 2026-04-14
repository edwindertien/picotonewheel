#pragma once
#include <stdint.h>
#include "config.h"
#include "wavetable.h"

// ============================================================
//  oscillator.h  –  Phase-accumulator oscillator
//
//  This is the core primitive of the tonewheel engine.
//  One Oscillator instance = one tonewheel.
//
//  The 32-bit phase accumulator wraps naturally at 2^32 giving
//  a seamless, jitter-free sine cycle at any frequency.
// ============================================================

struct Oscillator {
    uint32_t phase;      // current phase  (0 … 2^32-1)
    uint32_t phaseInc;   // added each sample = freq * 2^32 / Fs
    int16_t  ampI;       // amplitude pre-scaled: 0..256 (256 = full scale)
    bool     active;

    Oscillator() : phase(0), phaseInc(0), ampI(256), active(false) {}

    void setFrequency(float freq_hz) {
        phaseInc = (uint32_t)(freq_hz * (4294967296.0f / SAMPLE_RATE));
    }

    // Set amplitude from float 0..1 → pre-scale to integer 0..256
    void setAmplitude(float a) {
        ampI = (int16_t)(a * 256.0f);
    }

    // Tick: integer multiply, no float in the hot path
    inline int16_t tick() {
        if (!active) return 0;
        phase += phaseInc;
        uint16_t idx = (uint16_t)(phase >> PHASE_SHIFT);
        // sineTable[idx] is int16 ±32767, ampI is 0..256
        // product fits in int32, shift back by 8 → int16 range
        return (int16_t)((sineTable[idx] * (int32_t)ampI) >> 8);
    }
};

// ============================================================
//  Tonewheel frequency table
//
//  A real Hammond has 91 tonewheels.  Their frequencies derive
//  from the equal-temperament formula but were physically fixed
//  by gear ratios – so they deviate slightly from pure ET.
//  For now we use exact ET: f = 440 * 2^((n-69)/12)
//  (MIDI note 12 = C0,  note 36 = C2, etc.)
//
//  We generate the full 91-wheel set starting from the lowest
//  useful note (MIDI 24 = C1, ~32.7 Hz) up to note 114 (~7040 Hz).
// ============================================================
#define NUM_TONEWHEELS 91
#define TONEWHEEL_BASE_NOTE 24   // MIDI note number of wheel 0

inline float tonewheelFrequency(int wheelIndex)
{
    int midiNote = TONEWHEEL_BASE_NOTE + wheelIndex;
    return 440.0f * powf(2.0f, (midiNote - 69) / 12.0f);
}