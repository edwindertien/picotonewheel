#pragma once
// ============================================================
//  voice.h  –  Single monophonic voice (one note at a time)
//
//  Wraps one Oscillator with:
//    - MIDI note → frequency conversion (equal temperament)
//    - Pitch bend (±2 semitones default range)
//    - Simple amplitude envelope: instant on, instant off
//      (organ style — no attack/release needed for now)
//
//  Polyphony is added in the next step by replacing this with
//  a VoiceManager that holds multiple Voice instances.
// ============================================================

#include "oscillator.h"
#include <math.h>

// Pitch bend range in semitones (±2 is standard for organ)
#define PITCH_BEND_RANGE_ST  2.0f

class Voice {
public:
    Oscillator osc;
    uint8_t    currentNote = 255;   // 255 = no note
    float      pitchBend   = 0.0f; // semitones, -RANGE..+RANGE
    float      baseFreq    = 0.0f;

    // MIDI note number → equal temperament frequency
    static float noteToFreq(uint8_t note) {
        return 440.0f * powf(2.0f, (note - 69) / 12.0f);
    }

    void noteOn(uint8_t note, uint8_t velocity) {
        currentNote = note;
        baseFreq    = noteToFreq(note);
        _updateFreq();
        // Scale amplitude by velocity (organ doesn't normally do this,
        // but useful for testing; set to fixed 0.7f if you prefer)
        osc.amplitude = velocity / 127.0f * 0.8f;
        osc.active    = true;
    }

    void noteOff(uint8_t note) {
        if (note == currentNote) {
            osc.active  = false;
            currentNote = 255;
        }
    }

    // bend: -8192..+8191 (raw MIDI pitch bend value)
    void setPitchBend(int16_t bend) {
        pitchBend = (float)bend / 8192.0f * PITCH_BEND_RANGE_ST;
        if (currentNote != 255) _updateFreq();
    }

    inline int16_t tick() {
        return osc.tick();
    }

private:
    void _updateFreq() {
        // Apply pitch bend: freq * 2^(semitones/12)
        float freq = baseFreq * powf(2.0f, pitchBend / 12.0f);
        osc.setFrequency(freq);
    }
};