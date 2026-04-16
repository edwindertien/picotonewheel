#pragma once
// ============================================================
//  percussion.h  —  Hammond-style percussion  (Pico 2 version)
//  RP2350 FPU: float multiply is single-cycle — no integer tricks needed.
//  Envelope is a simple float that decays each sample.
// ============================================================

#include "oscillator.h"
#include "config.h"
#include <math.h>

#define PERC_CC_ONOFF    MIDI_CC_PERC_ONOFF
#define PERC_CC_HARMONIC MIDI_CC_PERC_HARMONIC
#define PERC_CC_DECAY    MIDI_CC_PERC_DECAY
#define PERC_CC_LEVEL    MIDI_CC_PERC_LEVEL

// Decay time constants: envelope multiplied each sample
// fast ~0.3 s: exp(-1/(0.3*44100))
// slow ~1.0 s: exp(-1/(1.0*44100))
static const float PERC_DECAY_FAST   = 0.999924f;
static const float PERC_DECAY_SLOW   = 0.999977f;
static const float PERC_LEVEL_NORMAL = 1.0f;
static const float PERC_LEVEL_SOFT   = 0.5f;

class Percussion {
public:
    bool enabled       = false;
    bool thirdHarmonic = false;
    bool fast          = true;
    bool soft          = false;

    void init() {
        _env        = 0.0f;
        _keysHeld   = 0;
        _triggered  = false;
        _osc.active = false;
        _osc.setAmplitude(1.0f);
    }

    bool noteOn(uint8_t note, int keysNowHeld) {
        _keysHeld = keysNowHeld;
        if (!enabled) return false;
        if (keysNowHeld == 1 && !_triggered) {
            _trigger(note);
            return true;
        }
        return false;
    }

    void noteOff(int keysNowHeld) {
        _keysHeld = keysNowHeld;
        if (keysNowHeld == 0) {
            _triggered  = false;
            _osc.active = false;
            _env        = 0.0f;
        }
    }

    // Float tick — FPU makes this essentially free on RP2350
    inline int16_t tick() {
        if (!_osc.active || _env < 0.001f) {
            _osc.active = false;
            return 0;
        }
        float level = soft ? PERC_LEVEL_SOFT : PERC_LEVEL_NORMAL;
        _osc.setAmplitude(_env * level);
        _env *= fast ? PERC_DECAY_FAST : PERC_DECAY_SLOW;
        return _osc.tick();
    }

    bool handleCC(uint8_t cc, uint8_t value) {
        switch (cc) {
            case PERC_CC_ONOFF:
                enabled = (value >= 64);
                if (!enabled) { _osc.active = false; _env = 0.0f; }
                return true;
            case PERC_CC_HARMONIC: thirdHarmonic = (value >= 64); return true;
            case PERC_CC_DECAY:    fast = (value >= 64);          return true;
            case PERC_CC_LEVEL:    soft = (value >= 64);          return true;
        }
        return false;
    }

    void debugPrint() const {
        Serial.print("  Perc: ");
        Serial.print(enabled ? "ON " : "OFF ");
        Serial.print(thirdHarmonic ? "3rd " : "2nd ");
        Serial.print(fast ? "fast " : "slow ");
        Serial.println(soft ? "soft" : "norm");
    }

private:
    Oscillator _osc;
    float      _env       = 0.0f;
    int        _keysHeld  = 0;
    bool       _triggered = false;

    void _trigger(uint8_t note) {
        float baseFreq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
        float freq     = baseFreq * (thirdHarmonic ? 3.0f : 2.0f);
        if (freq < 22000.0f) {
            _osc.setFrequency(freq);
            _env        = 1.0f;
            _osc.active = true;
            _triggered  = true;
        }
    }
};