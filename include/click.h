#pragma once
// ============================================================
//  click.h  –  Hammond key click
//  Integer-only hot path — no float in tick()
//  CC 84: click level 0..127
// ============================================================

#include <Arduino.h>

#define CLICK_CC_LEVEL  84

class Click {
public:
    uint8_t level = 40;

    void init()    { _envI = 0; }
    void trigger() { if (level > 0) _envI = 65535u; }

    bool handleCC(uint8_t cc, uint8_t value) {
        if (cc == CLICK_CC_LEVEL) {
            level = (uint8_t)((uint16_t)value * 255 / 127);
            return true;
        }
        return false;
    }

    // Integer-only tick: LCG noise × decaying envelope
    inline int16_t tick() {
        if (_envI == 0) return 0;
        // Decay: 0.9887 in 16.16 fixed point = 64790
        _envI = (uint32_t)((uint64_t)_envI * 64790u >> 16);
        if (_envI < 64) { _envI = 0; return 0; }
        _rng = _rng * 1664525u + 1013904223u;
        int16_t noise = (int16_t)(_rng >> 16);
        // (noise * level) >> 8 scales to ±32767 range
        int32_t out = ((int32_t)noise * level) >> 8;
        // Apply envelope: envI is 0..65535, shift to 0..255
        out = (out * (int32_t)(_envI >> 8)) >> 8;
        return (int16_t)out;
    }

    void debugPrint() const {
        Serial.print("  Click level: "); Serial.println(level);
    }

private:
    uint32_t _envI = 0;
    uint32_t _rng  = 0xDEADBEEFu;
};