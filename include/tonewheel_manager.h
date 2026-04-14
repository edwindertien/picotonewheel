#pragma once
// ============================================================
//  tonewheel_manager.h  –  Polyphonic tonewheel voice manager
//
//  Manages up to MAX_TW_VOICES simultaneous TonewheelVoice
//  instances, each with 9 oscillators.
//
//  CPU budget check at 133 MHz, 44100 Hz:
//    16 voices × 9 partials = 144 oscillators
//    Each tick: 1 phase increment + 1 table lookup + 1 multiply
//    144 × 44100 = ~6.35M ops/sec  →  well within budget
//
//  Drawbar updates propagate to all active voices immediately
//  so turning a drawbar while playing updates the timbre live.
// ============================================================

#include "tonewheel_voice.h"
#include "drawbars.h"

#define MAX_TW_VOICES  16

class TonewheelManager {
public:
    Drawbars drawbars;

    void init() {
        drawbars.init();
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            _voices[i].active      = false;
            _voices[i].currentNote = 255;
            _age[i]                = 0;
        }
        _clock       = 0;
        _activeCount = 0;
    }

    void noteOn(uint8_t note, uint8_t velocity) {
        // Ignore duplicate notes (organ behaviour)
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) return;
        }
        int slot = _findFreeVoice();
        _voices[slot].noteOn(note, velocity, drawbars);
        _age[slot]   = ++_clock;
        _activeCount = _countActive();
    }

    void noteOff(uint8_t note) {
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                _voices[i].noteOff(note);
                _age[i]      = 0;
                _activeCount = _countActive();
                return;
            }
        }
    }

    void setPitchBend(int16_t bend) {
        float st = (float)bend / 8192.0f * 2.0f;  // ±2 semitones
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            _voices[i].setPitchBend(st, drawbars);
        }
    }

    // Explicitly propagate current drawbar state to all active voices.
    // Call after directly modifying drawbars.level[].
    void propagateDrawbars() {
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active) _voices[i].updateDrawbars(drawbars);
        }
    }

    // Handle MIDI CC — routes drawbar CCs, returns true if consumed
    bool handleCC(uint8_t cc, uint8_t value) {
        if (!drawbars.handleCC(cc, value)) return false;
        // Propagate new drawbar settings to all active voices
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active) {
                _voices[i].updateDrawbars(drawbars);
            }
        }
        return true;
    }

    // Mix all voices. Call exactly once per sample.
    inline int16_t tick() {
        int32_t mix = 0;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            mix += _voices[i].tick();
        }
        // Each voice outputs max ±(32767 * 256 >> 8) / 10 = ±3276
        // 16 voices × 3276 = 52,416 — divide by 2 still clips at full polyphony.
        // Divide by 3 instead: 52416/3 = 17472, safely within int16.
        // In practice players rarely hold 16 notes at full drawbars;
        // a master volume CC will be added to fine-tune later.
        return (int16_t)(mix / 3);
    }

    uint8_t activeCount()  const { return _activeCount; }

    void allNotesOff() {
        for (int n = 0; n < 128; n++) noteOff(n);
    }

    void debugPrint() const {
        drawbars.debugPrint();
        Serial.print("  Active voices: "); Serial.println(_activeCount);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active) {
                Serial.print("    note=");
                Serial.println(_voices[i].currentNote);
            }
        }
    }

private:
    TonewheelVoice _voices[MAX_TW_VOICES];
    uint32_t       _age[MAX_TW_VOICES];
    uint32_t       _clock;
    uint8_t        _activeCount;

    int _findFreeVoice() {
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (!_voices[i].active) return i;
        }
        // Steal oldest
        int      oldest = 0;
        uint32_t minAge = _age[0];
        for (int i = 1; i < MAX_TW_VOICES; i++) {
            if (_age[i] < minAge) { minAge = _age[i]; oldest = i; }
        }
        return oldest;
    }

    uint8_t _countActive() const {
        uint8_t n = 0;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active) n++;
        }
        return n;
    }
};