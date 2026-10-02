#pragma once
// ============================================================
//  tonewheel_voice.h  –  One organ key = 9 tonewheel partials
//
//  A real Hammond tonewheel organ has 91 physical wheels.
//  Each key draws from up to 9 of them (one per drawbar).
//  We model this with 9 phase-accumulator oscillators per voice.
//
//  The frequencies of the 9 partials are:
//    partial_freq[i] = note_freq × DRAWBAR_MULT[i]
//
//  The amplitude of each partial = drawbar level / 8.0
//
//  getSample() sums all 9 oscillators weighted by drawbar level,
//  then scales the result so full drawbars (all 8s) still fits
//  in int16_t range without clipping.
//
//  Scaling: maximum theoretical sum with all drawbars at 8 =
//  9 × 32767 × 1.0 = 294,903. Divide by 9 to normalise → 32767.
//  We use /10 for a small headroom margin.
// ============================================================

#include <stdint.h>
#include <math.h>
#include "config.h"
#include "wavetable.h"     // sineTable, PHASE_SHIFT
#include "drawbars.h"

// Performance notes (this file is the hot path, ~all of the CPU at high polyphony):
//  - Only partials that actually sound (drawbar level > 0, below 22 kHz) are
//    iterated; a compact index list is rebuilt whenever drawbars/bend change.
//  - tickRaw() returns the *unscaled* sum of sine*amplitude (int32). The
//    manager applies one scale factor for all voices (1/7680), instead of a
//    shift per partial and a divide per voice.
//  - Vibrato is not computed per oscillator per sample: the manager calls
//    applyVibrato() once every VIBRATO_BLOCK samples with an integer
//    multiplier. Phase accumulators stay continuous, so there are no clicks.

// Vibrato multiplier format: unsigned Q30 (1.0 == 1<<30). 30 bits keep the
// error far below what matters even for the highest partials.
#define VIB_ONE  (1u << 30)

class TonewheelVoice {
public:
    uint8_t  currentNote = 255;
    float    pitchBend   = 0.0f;   // semitones
    bool     active      = false;  // contributes to the output (held OR fading out)
    bool     fading      = false;  // being stolen: fading out, no longer a held note

    void noteOn(uint8_t note, uint8_t velocity,
                const Drawbars& db)
    {
        currentNote = note;
        active      = true;
        fading      = false;
        _baseFreq   = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        _updatePartials(db);
        // Organ: ignore velocity (but keep it for optional click control later)
        (void)velocity;
    }

    // Normal key release: instant cut (like a real Hammond; the key click covers it)
    void noteOff(uint8_t note) {
        if (note == currentNote) silence();
    }

    // Immediately silence this voice, whatever state it is in
    void silence() {
        active      = false;
        fading      = false;
        currentNote = 255;
        _n          = 0;
    }

    // Graceful steal: fade out over VOICE_FADE_SAMPLES instead of cutting.
    // The voice stops counting as a held note immediately.
    void startFade() {
        if (!active || fading) return;
        fading      = true;
        currentNote = 255;
        _fade       = 65536;
    }

    // Call when any drawbar level changes while note is held
    void updateDrawbars(const Drawbars& db) {
        if (active) _updatePartials(db);
    }

    void setPitchBend(float semitones, const Drawbars& db) {
        pitchBend = semitones;
        if (active) _updatePartials(db);
    }

    uint8_t partialCount() const { return active ? _n : 0; }

    // ---- Audio path (core 1) ------------------------------------------

    // Unscaled sum of sine*amplitude over all sounding partials.
    // Full scale: 32767 * 256 per partial. Called exactly once per sample.
    __attribute__((always_inline)) inline int32_t tickRaw()
    {
        if (!active) return 0;
        int32_t acc = 0;
        const uint8_t n = _n;
        for (uint8_t k = 0; k < n; k++) {
            Partial& p = _p[_idx[k]];
            p.phase += p.inc;
            acc += (int32_t)sineTable[p.phase >> PHASE_SHIFT] * p.amp;
        }
        if (fading) {
            acc = (int32_t)(((int64_t)acc * _fade) >> 16);
            _fade -= (65536 / VOICE_FADE_SAMPLES);
            if (_fade <= 0) silence();
        }
        return acc;
    }

    // Vibrato: scale every partial's phase increment by q / VIB_ONE.
    // Called once per VIBRATO_BLOCK samples (not per sample). q == VIB_ONE -> unmodulated.
    inline void applyVibrato(uint32_t q)
    {
        _vq = q;
        const uint8_t n = _n;
        for (uint8_t k = 0; k < n; k++) {
            Partial& p = _p[_idx[k]];
            p.inc = (uint32_t)(((uint64_t)p.baseInc * q) >> 30);
        }
    }

    // Set the current vibrato state before noteOn() so a new voice starts modulated
    void setVibratoState(uint32_t q) { _vq = q; }

private:
    // One 16-byte record per drawbar partial: a single base register and a
    // shift-by-4 index reach all fields in the inner loop.
    struct Partial {
        uint32_t phase;      // persists across notes (indexed by drawbar)
        uint32_t inc;        // phase increment incl. vibrato
        uint32_t baseInc;    // unmodulated phase increment
        int16_t  amp;        // 0..256
        int16_t  pad;
    };
    Partial  _p[NUM_DRAWBARS]   = {};
    uint8_t  _idx[NUM_DRAWBARS] = {0,1,2,3,4,5,6,7,8};   // sounding partials
    uint8_t  _n        = 0;
    int32_t  _fade     = 65536;             // Q16 fade gain while fading
    uint32_t _vq       = VIB_ONE;           // current vibrato multiplier (Q30)
    float    _baseFreq = 440.0f;

    void _updatePartials(const Drawbars& db) {
        const float rootFreq = _baseFreq * powf(2.0f, pitchBend / 12.0f);
        uint8_t n = 0;
        uint8_t idx[NUM_DRAWBARS];
        for (int i = 0; i < NUM_DRAWBARS; i++) {
            const float freq = rootFreq * DRAWBAR_MULT[i];
            if (freq >= 22000.0f || db.level[i] == 0) {
                _p[i].amp = 0;
            } else {
                _p[i].baseInc = (uint32_t)(freq * (4294967296.0f / SAMPLE_RATE));
                _p[i].inc     = _p[i].baseInc;
                _p[i].amp     = (int16_t)(db.amplitude(i) * 256.0f);
                idx[n++]      = (uint8_t)i;
            }
        }
        // Publish the list. Entries are always valid drawbar indices (0..8), so a
        // concurrent read from core 1 can at worst glitch one sample, never crash.
        for (uint8_t k = 0; k < n; k++) _idx[k] = idx[k];
        _n = n;
        if (_vq != VIB_ONE) applyVibrato(_vq);   // keep the current vibrato state
    }
};