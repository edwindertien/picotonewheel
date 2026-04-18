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

#include "oscillator.h"
#include "drawbars.h"
#include <math.h>

class TonewheelVoice {
public:
    uint8_t  currentNote = 255;
    float    pitchBend   = 0.0f;   // semitones
    bool     active      = false;

    void noteOn(uint8_t note, uint8_t velocity,
                const Drawbars& db)
    {
        currentNote = note;
        active      = true;
        _baseFreq   = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        _updatePartials(db);
        // Organ: ignore velocity (but keep it for optional click control later)
        (void)velocity;
    }

    void noteOff(uint8_t note) {
        if (note == currentNote) {
            active      = false;
            currentNote = 255;
            for (int i = 0; i < NUM_DRAWBARS; i++) {
                _osc[i].active = false;
            }
        }
    }

    // Call when any drawbar level changes while note is held
    void updateDrawbars(const Drawbars& db) {
        if (active) _updatePartials(db);
    }

    void setPitchBend(float semitones, const Drawbars& db) {
        pitchBend = semitones;
        if (active) _updatePartials(db);
    }

    // Tick all 9 oscillators and return the mixed sample.
    // pitchMult: vibrato multiplier from Vibrato::pitchMult (1.0 = no shift).
    // Called exactly once per sample.
    inline int16_t tick(float pitchMult = 1.0f)
    {
        if (!active) return 0;
        int32_t mix = 0;
        for (int i = 0; i < NUM_DRAWBARS; i++) {
            if (pitchMult != 1.0f) _osc[i].applyPitchMult(pitchMult);
            mix += _osc[i].tick();
        }
        // Divide by 10 for headroom (9 partials at full scale)
        return (int16_t)(mix / 10);
    }

private:
    Oscillator _osc[NUM_DRAWBARS];
    float      _baseFreq = 440.0f;

    void _updatePartials(const Drawbars& db) {
        float rootFreq = _baseFreq * powf(2.0f, pitchBend / 12.0f);
        for (int i = 0; i < NUM_DRAWBARS; i++) {
            float freq = rootFreq * DRAWBAR_MULT[i];
            if (freq >= 22000.0f) {
                _osc[i].active = false;
                _osc[i].setAmplitude(0.0f);
            } else {
                _osc[i].setFrequency(freq);
                _osc[i].setAmplitude(db.amplitude(i));
                _osc[i].active = (db.level[i] > 0);
            }
        }
    }
};