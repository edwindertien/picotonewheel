#pragma once
// ============================================================
//  percussion.h  –  Hammond-style percussion
//
//  One global percussion oscillator with exponential decay.
//  Fires only on the first note of a new chord (all-keys-up
//  then first key-down). Subsequent notes in the same chord
//  do not re-trigger percussion.
//
//  Controls (all set directly or via MIDI CC):
//    enabled   : on/off
//    harmonic  : PERC_2ND (×2) or PERC_3RD (×3)
//    fast      : true=fast decay (~0.3s), false=slow (~1.0s)
//    soft      : true=soft level, false=normal level
//
//  MIDI CC assignments (defaults):
//    CC 80 : percussion on/off   (value ≥ 64 = on)
//    CC 81 : harmonic 2nd/3rd    (value ≥ 64 = 3rd)
//    CC 82 : decay fast/slow     (value ≥ 64 = fast)
//    CC 83 : level soft/normal   (value ≥ 64 = soft)
// ============================================================

#include "oscillator.h"
#include <math.h>

#define PERC_CC_ONOFF    80
#define PERC_CC_HARMONIC 81
#define PERC_CC_DECAY    82
#define PERC_CC_LEVEL    83

// Fixed-point decay coefficients (16.16 format, i.e. value/65536 per sample)
// fast ~0.3s: coeff = exp(-1/(0.3*44100)) ≈ 0.999924 → 0.999924*65536 = 65530
// slow ~1.0s: coeff = exp(-1/(1.0*44100)) ≈ 0.999977 → 0.999977*65536 = 65534
static const uint32_t DECAY_FAST_FP = 65530u;
static const uint32_t DECAY_SLOW_FP = 65534u;

class Percussion {
public:
    bool    enabled       = false;
    bool    thirdHarmonic = false;
    bool    fast          = true;
    bool    soft          = false;

    // expose private booleans as aliases for tick()
    bool _fast = true;
    bool _soft = false;

    void init() {
        _envI       = 0;
        _keysHeld   = 0;
        _triggered  = false;
        _osc.active = false;
        _osc.ampI   = 255;
    }

    // Call on every note-on. Returns true if percussion triggered.
    bool noteOn(uint8_t note, int keysNowHeld) {
        _keysHeld = keysNowHeld;
        if (!enabled) return false;

        // Only trigger if this is the first note of a new chord
        // i.e. no keys were held before this note-on
        if (keysNowHeld == 1 && !_triggered) {
            _trigger(note);
            return true;
        }
        return false;
    }

    // Call on every note-off.
    void noteOff(int keysNowHeld) {
        _keysHeld = keysNowHeld;
        if (keysNowHeld == 0) {
            _triggered  = false;
            _osc.active = false;
            _envI       = 0;
        }
    }

    // Tick: advance envelope (integer path, no float multiply in hot path)
    inline int16_t tick() {
        if (!_osc.active || _envI == 0) {
            _osc.active = false;
            return 0;
        }
        // Advance envelope: multiply by decay coefficient (fixed point 0..65536)
        uint32_t dc = _fast ? DECAY_FAST_FP : DECAY_SLOW_FP;
        _envI = (uint32_t)((uint64_t)_envI * dc >> 16);
        if (_envI < 32) { _osc.active = false; return 0; }

        // Scale amplitude by envelope and soft level
        // _envI is 0..65535; soft halves it
        uint32_t amp = _soft ? (_envI >> 1) : _envI;
        _osc.ampI = (int16_t)(amp >> 8);  // 0..255
        return _osc.tick();
    }

    // Handle MIDI CC. Returns true if consumed.
    bool handleCC(uint8_t cc, uint8_t value) {
        switch (cc) {
            case PERC_CC_ONOFF:
                enabled = (value >= 64);
                if (!enabled) { _osc.active = false; _envI = 0; }
                return true;
            case PERC_CC_HARMONIC:
                thirdHarmonic = (value >= 64);
                return true;
            case PERC_CC_DECAY:
                fast = _fast = (value >= 64);
                return true;
            case PERC_CC_LEVEL:
                soft = _soft = (value >= 64);
                return true;
        }
        return false;
    }

    void debugPrint() const {
        Serial.print("  Percussion: ");
        Serial.print(enabled ? "ON  " : "OFF ");
        Serial.print(thirdHarmonic ? "3rd  " : "2nd  ");
        Serial.print(fast ? "fast  " : "slow  ");
        Serial.println(soft ? "soft" : "normal");
    }

private:
    Oscillator _osc;
    uint32_t   _envI      = 0;     // fixed-point envelope 0..65535
    int        _keysHeld  = 0;
    bool       _triggered = false;

    void _trigger(uint8_t note) {
        float baseFreq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        float freq     = baseFreq * (thirdHarmonic ? 3.0f : 2.0f);
        if (freq < 22000.0f) {
            _fast       = fast;
            _soft       = soft;
            _osc.setFrequency(freq);
            _envI       = 65535u;
            _osc.active = true;
            _triggered  = true;
        }
    }
};