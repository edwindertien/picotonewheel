#pragma once
// ============================================================
//  click.h  —  Key click  (Pico 2 version)
//  Float envelope — free on RP2350 FPU
// ============================================================

#include "config.h"

#define CLICK_CC_LEVEL  MIDI_CC_CLICK

// Decay: ~5ms burst. exp(-1/(0.005*44100)) ≈ 0.9955
static const float CLICK_DECAY = 0.9955f;

class Click {
public:
    uint8_t level = 40;

    void init()    { _env = 0.0f; }
    void trigger() { if (level > 0) _env = 1.0f; }

    bool handleCC(uint8_t cc, uint8_t value) {
        if (cc == CLICK_CC_LEVEL) {
            level = (uint8_t)((uint16_t)value * 255 / 127);
            return true;
        }
        return false;
    }

    inline int16_t tick() {
        if (_env < 0.002f) { _env = 0.0f; return 0; }
        _rng = _rng * 1664525u + 1013904223u;
        int16_t noise = (int16_t)(_rng >> 16);
        // FPU: float multiply is single-cycle
        int16_t out = (int16_t)(noise * (level / 255.0f) * _env);
        _env *= CLICK_DECAY;
        return out;
    }

    void debugPrint() const {
        Serial.print("  Click level: "); Serial.println(level);
    }

private:
    float    _env = 0.0f;
    uint32_t _rng = 0xDEADBEEFu;
};