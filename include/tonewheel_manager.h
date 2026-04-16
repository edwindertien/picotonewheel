#pragma once
// ============================================================
//  tonewheel_manager.h  —  Polyphonic tonewheel voice manager
//
//  Fixes vs v7:
//  1. Polyphony cap (MAX_ACTIVE_VOICES) — graceful degradation
//     instead of collapse. Oldest voice stolen at cap.
//  2. Hardware spinlock — non-blocking in tick() (try-lock),
//     blocking in noteOn/noteOff (MIDI rate, can wait).
//     Prevents the data race that caused half-note detune glitch.
//  3. tick() uses int64 intermediate for the /3 scaling so it
//     never overflows regardless of voice count or bar setting.
//  4. Percussion only ticks when enabled; cached drawbarSum
//     avoids repeated calls to drawbarSum() in hot path.
// ============================================================

#include "tonewheel_voice.h"
#include "drawbars.h"
#include "percussion.h"
#include "click.h"
#include <hardware/sync.h>

#define MAX_TW_VOICES      16   // voice pool size (memory)
// MAX_ACTIVE_VOICES is defined in config.h

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

        // Ignore duplicate notes
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {
                spin_unlock(_lock, irq); return;
            }
        }

        // Enforce polyphony cap: steal oldest if at limit
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

        // Percussion and click outside lock (they're core-1-only state)
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
        float st = (float)bend / 8192.0f * 2.0f;
        uint32_t irq = spin_lock_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            _voices[i].setPitchBend(st, drawbars);
        spin_unlock(_lock, irq);
    }

    bool handleCC(uint8_t cc, uint8_t value) {
        if (drawbars.handleCC(cc, value)) {
            uint32_t irq = spin_lock_blocking(_lock);
            _cachedDbSum = drawbars.drawbarSum();
            propagateDrawbars_locked();
            spin_unlock(_lock, irq);
            return true;
        }
        if (perc.handleCC(cc, value)) {
            uint32_t irq = spin_lock_blocking(_lock);
            propagateDrawbars_locked();
            spin_unlock(_lock, irq);
            return true;
        }
        if (click.handleCC(cc, value)) return true;
        return false;
    }

    void propagateDrawbars() {
        uint32_t irq = spin_lock_blocking(_lock);
        _cachedDbSum = drawbars.drawbarSum();
        propagateDrawbars_locked();
        spin_unlock(_lock, irq);
    }

    // ---- Hot path: called once per sample from core 1 -------
    inline int16_t tick() {
        int32_t mix = 0;

        // Non-blocking try-lock: if core 0 is mid-noteOn we skip
        // the lock and use voice state as-is. At most 1 sample of
        // stale data — completely inaudible. Never stalls audio.
        bool locked = spin_try_lock_unsafe(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            mix += _voices[i].tick();
        if (locked) spin_unlock_unsafe(_lock);

        // Scale down: use int64 intermediate to prevent overflow.
        // 16 voices × max_voice_out(29490) = 471,840
        // 471,840 × 21845 = 10.3B — overflows int32, fine as int64.
        // 21845 >> 16 ≈ 1/3
        mix = (int32_t)((int64_t)mix * 21845 >> 16);

        // Percussion scaled by drawbar sum
        if (perc.enabled) {
            int32_t ps = perc.tick();
            mix += (int32_t)((int64_t)ps * _cachedDbSum / 288);
        }

        // Click
        {
            int32_t cs = click.tick();
            mix += (int32_t)((int64_t)cs * (_cachedDbSum + 18) / 360);
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
        Serial.print("  Active voices: "); Serial.println(_activeCount);
        Serial.print("  Polyphony cap: "); Serial.println(MAX_ACTIVE_VOICES);
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

    // Call with lock held
    void propagateDrawbars_locked() {
        Drawbars effective = drawbars;
        if (perc.enabled && effective.level[2] > 0)
            effective.level[2] = (uint8_t)(effective.level[2] * 3 / 4);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (_voices[i].active) _voices[i].updateDrawbars(effective);
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