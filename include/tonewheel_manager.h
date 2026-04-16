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
        uint32_t irq = spin_lock_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                spin_unlock(_lock, irq); return;
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
        spin_unlock(_lock, irq);

        perc.noteOn(note, _activeCount);
        click.trigger();
    }

    void noteOff(uint8_t note) {
        uint32_t irq = spin_lock_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                _voices[i].noteOff(note);
                _age[i]      = 0;
                _activeCount = _countActive();
                spin_unlock(_lock, irq);
                click.trigger();
                perc.noteOff(_activeCount);
                return;
            }
        }
        spin_unlock(_lock, irq);
    }

    void setPitchBend(int16_t bend) {
        float st = bend / 8192.0f * 2.0f;
        uint32_t irq = spin_lock_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            _voices[i].setPitchBend(st, drawbars);
        spin_unlock(_lock, irq);
    }

    bool handleCC(uint8_t cc, uint8_t value) {
        if (drawbars.handleCC(cc, value)) {
            uint32_t irq = spin_lock_blocking(_lock);
            _cachedDbSum = drawbars.drawbarSum();
            _propagate();
            spin_unlock(_lock, irq);
            return true;
        }
        if (perc.handleCC(cc, value)) {
            uint32_t irq = spin_lock_blocking(_lock);
            _propagate();
            spin_unlock(_lock, irq);
            return true;
        }
        if (click.handleCC(cc, value)) return true;
        return false;
    }

    void propagateDrawbars() {
        uint32_t irq = spin_lock_blocking(_lock);
        _cachedDbSum = drawbars.drawbarSum();
        _propagate();
        spin_unlock(_lock, irq);
    }

    // ---- Audio hot path — core 1 ----------------------------
    inline int16_t tick() {
        int32_t mix = 0;

        // Non-blocking try-lock: never stalls audio core
        bool locked = spin_try_lock_unsafe(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            mix += _voices[i].tick();
        if (locked) spin_unlock_unsafe(_lock);

        // RP2350 has hardware divide — just use /
        // No reciprocal-multiply approximations needed
        mix /= 3;

        // Percussion scaled by drawbar sum
        // FPU means float envelope in perc.tick() is free
        if (perc.enabled) {
            int32_t ps = perc.tick();
            if (_cachedDbSum > 0)
                mix += (ps * _cachedDbSum) / 288;
        }

        // Click
        {
            int32_t cs = click.tick();
            mix += (cs * (_cachedDbSum + 18)) / 360;
        }

        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        return (int16_t)mix;
    }

    uint8_t activeCount() const { return _activeCount; }

    void allNotesOff() {
        uint32_t irq = spin_lock_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active)
                _voices[i].noteOff(_voices[i].currentNote);
            _age[i] = 0;
        }
        _activeCount = 0;
        spin_unlock(_lock, irq);
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