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
    uint32_t phase;     // current phase  (0 … 2^32-1)
    uint32_t phaseInc;  // added each sample = freq * 2^32 / Fs
    float    amplitude; // 0.0 … 1.0  (set by drawbar level)
    bool     active;    // gate: true while key is held

    Oscillator() : phase(0), phaseInc(0), amplitude(0.0f), active(false) {}

    // Call once when the desired frequency changes (or at init).
    // freq_hz : tonewheel frequency in Hz
    void setFrequency(float freq_hz)
    {
        // phase_inc = freq / Fs * 2^32
        phaseInc = (uint32_t)(freq_hz * (4294967296.0f / SAMPLE_RATE));
    }

    // Call every sample tick.  Returns the next 16-bit sample,
    // scaled by amplitude.  Returns 0 when inactive.
    inline int16_t tick()
    {
        if (!active) return 0;

        // Advance phase
        phase += phaseInc;

        // Top WAVETABLE_BITS bits index into the sine table
        uint16_t idx = (uint16_t)(phase >> PHASE_SHIFT);

        // Scale and return
        return (int16_t)(sineTable[idx] * amplitude);
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
