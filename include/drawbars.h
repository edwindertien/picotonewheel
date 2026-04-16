#pragma once
// ============================================================
//  drawbars.h  –  9-drawbar state
//
//  Each drawbar has a level 0..8 (matching the physical
//  markings on a real Hammond).  Level 8 = full amplitude.
//
//  The harmonic multipliers are the footage ratios:
//    16'=0.5  5⅓'=1.5  8'=1  4'=2  2⅔'=3  2'=4  1⅗'=5  1⅓'=6  1'=8
//
//  Drawbar levels can be set:
//    - Directly (for testing / preset recall)
//    - Via MIDI CC (CC 12..20 by default, matches typical mappings)
// ============================================================

#include <Arduino.h>

#define NUM_DRAWBARS  9

// MIDI CC numbers for the 9 drawbars (customise if needed)
static const uint8_t DRAWBAR_CC[NUM_DRAWBARS] = {
    12, 13, 14, 15, 16, 17, 18, 19, 20
};

// Harmonic multipliers for each drawbar (frequency = note_freq × mult)
static const float DRAWBAR_MULT[NUM_DRAWBARS] = {
    0.5f,   // 1: 16'   sub-octave
    1.5f,   // 2: 5⅓'  sub-fifth (nazard)
    1.0f,   // 3: 8'    unison (principal)
    2.0f,   // 4: 4'    octave
    3.0f,   // 5: 2⅔'  quint
    4.0f,   // 6: 2'    super-octave
    5.0f,   // 7: 1⅗'  tierce
    6.0f,   // 8: 1⅓'  larigot
    8.0f,   // 9: 1'    sifflöte
};

// Drawbar name strings for debug output
static const char* DRAWBAR_NAME[NUM_DRAWBARS] = {
    "16'  ", "5-1/3'", "8'   ", "4'   ",
    "2-2/3'", "2'   ", "1-3/5'", "1-1/3'", "1'   "
};

struct Drawbars {
    uint8_t level[NUM_DRAWBARS];

    // Initialise to a classic full-organ registration (all drawbars out)
    void init() {
        for (int i = 0; i < NUM_DRAWBARS; i++) level[i] = 8;
    }

    // Preset: all drawbars in (silence)
    void presetOff() {
        for (int i = 0; i < NUM_DRAWBARS; i++) level[i] = 0;
    }

    // Preset: classic gospel/rock (8' + harmonics)
    void presetFullOrgan() {
        // 888000000 — the "all fours" registration
        uint8_t p[NUM_DRAWBARS] = {8, 8, 8, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < NUM_DRAWBARS; i++) level[i] = p[i];
    }

    void presetJazz() {
        // 868800000 — full but focused
        uint8_t p[NUM_DRAWBARS] = {8, 6, 8, 8, 0, 0, 0, 0, 0};
        for (int i = 0; i < NUM_DRAWBARS; i++) level[i] = p[i];
    }

    void presetFlute() {
        // 004020000
        uint8_t p[NUM_DRAWBARS] = {0, 0, 4, 0, 2, 0, 0, 0, 0};
        for (int i = 0; i < NUM_DRAWBARS; i++) level[i] = p[i];
    }

    // Handle incoming MIDI CC
    // Returns true if the CC was a drawbar CC
    bool handleCC(uint8_t cc, uint8_t value) {
        for (int i = 0; i < NUM_DRAWBARS; i++) {
            if (cc == DRAWBAR_CC[i]) {
                // MIDI CC is 0..127, map to 0..8
                level[i] = (uint8_t)((value * 8 + 64) / 127);
                level[i] = min(level[i], (uint8_t)8);
                return true;
            }
        }
        return false;
    }

    // Amplitude for drawbar i, normalised 0..1
    float amplitude(int i) const {
        return level[i] / 8.0f;
    }

    // Sum of all drawbar levels (0..72).
    // Used to scale percussion proportionally to overall volume.
    int drawbarSum() const {
        int s = 0;
        for (int i = 0; i < NUM_DRAWBARS; i++) s += level[i];
        return s;
    }

    void debugPrint() const {
        Serial.print("  Drawbars: ");
        for (int i = 0; i < NUM_DRAWBARS; i++) {
            Serial.print(level[i]);
        }
        Serial.println();
    }
};