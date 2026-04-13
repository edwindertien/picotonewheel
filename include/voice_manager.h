#pragma once
// ============================================================
//  voice_manager.h  –  Polyphonic organ voice manager
//
//  Organ-specific polyphony rules:
//    - A note already ON is ignored (no re-trigger)
//    - Note-off only silences the voice holding that exact note
//    - All active voices are summed in getSample()
//    - If all voices are busy, the oldest voice is stolen
//
//  MAX_VOICES 16 comfortably covers one manual plus pedals.
// ============================================================

#include "voice.h"

#define MAX_VOICES  16

class VoiceManager {
public:

    void init() {
        for (int i = 0; i < MAX_VOICES; i++) {
            _voices[i].osc.active  = false;
            _voices[i].currentNote = 255;
            _age[i]                = 0;
        }
        _clock       = 0;
        _activeCount = 0;
    }

    void noteOn(uint8_t note, uint8_t velocity)
    {
        // Ignore if already playing (organ behaviour)
        for (int i = 0; i < MAX_VOICES; i++) {
            if (_voices[i].currentNote == note) return;
        }
        int slot = _findFreeVoice();
        _voices[slot].noteOn(note, velocity);
        _age[slot]   = ++_clock;
        _activeCount = _countActive();
    }

    void noteOff(uint8_t note)
    {
        for (int i = 0; i < MAX_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                _voices[i].noteOff(note);
                _age[i]      = 0;
                _activeCount = _countActive();
                return;
            }
        }
    }

    void setPitchBend(int16_t bend)
    {
        for (int i = 0; i < MAX_VOICES; i++) {
            _voices[i].setPitchBend(bend);
        }
    }

    // Advance all oscillators by ONE sample tick and return the mixed sample.
    // Call exactly once per sample period — do NOT call twice for L/R.
    inline int16_t tick()
    {
        int32_t mix = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            mix += _voices[i].tick();
        }
        // Divide by MAX_VOICES to prevent clipping.
        return (int16_t)(mix / MAX_VOICES);
    }

    uint8_t activeCount() const { return _activeCount; }

    void debugPrint() const
    {
        Serial.print("  Active voices: "); Serial.println(_activeCount);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (_voices[i].currentNote != 255) {
                float f = (float)_voices[i].osc.phaseInc
                          * 44100.0f / 4294967296.0f;
                Serial.print("    ["); Serial.print(i);
                Serial.print("] note="); Serial.print(_voices[i].currentNote);
                Serial.print("  ");     Serial.print(f, 1);
                Serial.println(" Hz");
            }
        }
    }

private:
    Voice    _voices[MAX_VOICES];
    uint32_t _age[MAX_VOICES];
    uint32_t _clock;
    uint8_t  _activeCount;

    int _findFreeVoice()
    {
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!_voices[i].osc.active) return i;
        }
        // Steal oldest
        int      oldest = 0;
        uint32_t minAge = _age[0];
        for (int i = 1; i < MAX_VOICES; i++) {
            if (_age[i] < minAge) { minAge = _age[i]; oldest = i; }
        }
        return oldest;
    }

    uint8_t _countActive() const
    {
        uint8_t n = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (_voices[i].osc.active) n++;
        }
        return n;
    }
};