#pragma once
// ============================================================
//  tonewheel_manager.h  —  Pico 2 optimised
//
//  RP2350 Cortex-M33 has:
//    - Hardware divide  → plain / operator, no workarounds
//    - Hardware FPU     → float arithmetic essentially free
//    - 520 KB SRAM      → no memory pressure
//
//  vs Pico 1 version:
//    - No reciprocal-multiply approximations (just use /)
//    - Float percussion/click envelopes restored (FPU makes free)
//    - Spinlock + try-lock pattern retained (still dual-core)
//    - Polyphony cap from config.h (default 16)
// ============================================================

#include "tonewheel_voice.h"
#include "drawbars.h"
#include "percussion.h"
#include "click.h"
#include "effects.h"
#include <hardware/sync.h>

#define MAX_TW_VOICES  20   // pool size — always >= MAX_ACTIVE_VOICES

class TonewheelManager {
public:
    Drawbars   drawbars;
    Percussion perc;
    Click      click;

    void init() {
        drawbars.init();
        perc.init();
        click.init();
        _cachedDbSum = 72;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            _voices[i].active      = false;
            _voices[i].currentNote = 255;
            _age[i]                = 0;
        }
        _clock       = 0;
        _activeCount = 0;
        _lock = spin_lock_init(spin_lock_claim_unused(true));
    }

    void noteOn(uint8_t note, uint8_t velocity) {
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                spin_unlock_unsafe(_lock); return;
            }
        }
        if (_countActive() >= MAX_ACTIVE_VOICES) {
            int oldest = _findOldestActive();
            _voices[oldest].noteOff(_voices[oldest].currentNote);
            _age[oldest] = 0;
        }
        int slot = _findFreeVoice();
        _voices[slot].noteOn(note, velocity, drawbars);
        _age[slot]   = ++_clock;
        _activeCount = _countActive();
        spin_unlock_unsafe(_lock);

        perc.noteOn(note, _activeCount);
        click.trigger();
    }

    void noteOff(uint8_t note) {
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                _voices[i].noteOff(note);
                _age[i]      = 0;
                _activeCount = _countActive();
                spin_unlock_unsafe(_lock);
                click.trigger();
                perc.noteOff(_activeCount);
                return;
            }
        }
        spin_unlock_unsafe(_lock);
    }

    void setPitchBend(int16_t bend) {
        float st = bend / 8192.0f * 2.0f;
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            _voices[i].setPitchBend(st, drawbars);
        spin_unlock_unsafe(_lock);
    }

    bool handleCC(uint8_t cc, uint8_t value) {
        if (drawbars.handleCC(cc, value)) {
            spin_lock_unsafe_blocking(_lock);
            _cachedDbSum = drawbars.drawbarSum();
            _propagate();
            spin_unlock_unsafe(_lock);
            return true;
        }
        if (perc.handleCC(cc, value)) {
            spin_lock_unsafe_blocking(_lock);
            _propagate();
            spin_unlock_unsafe(_lock);
            return true;
        }
        if (click.handleCC(cc, value)) return true;
        return false;
    }

    void propagateDrawbars() {
        spin_lock_unsafe_blocking(_lock);
        _cachedDbSum = drawbars.drawbarSum();
        _propagate();
        spin_unlock_unsafe(_lock);
    }

    uint8_t masterVolume = 255;   // 0–255, set via serial 'vol' or CC7

    // ---- Effects -------------------------------------------
    Overdrive overdrive;
    Vibrato   vibrato;
    Chorus    chorus;

    // Handle effects CCs — returns true if consumed
    bool handleFxCC(uint8_t cc, uint8_t value) {
        switch (cc) {
            case MIDI_CC_DRIVE:          overdrive.drive   = value; return true;
            case MIDI_CC_VIBRATO_DEPTH:  vibrato.depth     = value; return true;
            case MIDI_CC_VIBRATO_RATE:   vibrato.rate      = value; return true;
            case MIDI_CC_CHORUS_DEPTH:   chorus.depth      = value; return true;
            case MIDI_CC_CHORUS_RATE:    chorus.rate       = value; return true;
            case MIDI_CC_CHORUS_MIX:     chorus.mix        = value; return true;
            default: return false;
        }
    }

    // ---- Audio hot path — core 1 ----------------------------
    inline int16_t tick() {
        int32_t mix = 0;

        // 1. Vibrato LFO tick (updates pitchMult before voices read it)
        vibrato.tick();
        float pm = vibrato.pitchMult;

        bool locked = spin_try_lock_unsafe(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            mix += _voices[i].tick(pm);
        if (locked) spin_unlock_unsafe(_lock);

        mix /= 3;

        if (perc.enabled) {
            int32_t ps = perc.tick();
            if (_cachedDbSum > 0)
                mix += (ps * _cachedDbSum) / 288;
        }

        {
            int32_t cs = click.tick();
            mix += (cs * (_cachedDbSum + 18)) / 360;
        }

        // 2. Overdrive
        mix = overdrive.process(mix);

        // 3. Chorus
        mix = chorus.process(mix);

        // Master volume: scale by 0–255
        if (masterVolume < 255)
            mix = (mix * masterVolume) / 255;

        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        return (int16_t)mix;
    }

    uint8_t activeCount() const { return _activeCount; }

    void allNotesOff() {
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active)
                _voices[i].noteOff(_voices[i].currentNote);
            _age[i] = 0;
        }
        _activeCount = 0;
        spin_unlock_unsafe(_lock);
    }

    void debugPrint() const {
        drawbars.debugPrint();
        perc.debugPrint();
        click.debugPrint();
        Serial.print("  Active voices: "); Serial.print(_activeCount);
        Serial.print(" / "); Serial.println(MAX_ACTIVE_VOICES);
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
    uint32_t       _clock       = 0;
    uint8_t        _activeCount = 0;
    int            _cachedDbSum = 72;
    spin_lock_t*   _lock        = nullptr;

    void _propagate() {
        Drawbars eff = drawbars;
        if (perc.enabled && eff.level[2] > 0)
            eff.level[2] = (uint8_t)(eff.level[2] * 3 / 4);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (_voices[i].active) _voices[i].updateDrawbars(eff);
    }

    int _findFreeVoice() {
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (!_voices[i].active) return i;
        return _findOldestActive();
    }

    int _findOldestActive() {
        int oldest = 0;
        uint32_t minAge = 0xFFFFFFFFu;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active && _age[i] < minAge) {
                minAge = _age[i]; oldest = i;
            }
        }
        return oldest;
    }

    uint8_t _countActive() const {
        uint8_t n = 0;
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (_voices[i].active) n++;
        return n;
    }
};