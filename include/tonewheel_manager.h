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
#include "perf.h"
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
        voiceLimit   = MAX_ACTIVE_VOICES;
        _perOsc = 14.0f; _rest = 600.0f; _upCount = 0;
        _cycVoices = _cycOd = _cycChorus = _cycLeslie = 0;
        _lock = spin_lock_init(spin_lock_claim_unused(true));
    }

    void noteOn(uint8_t note, uint8_t velocity) {
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].currentNote == note) {      // fading voices have currentNote 255
                spin_unlock_unsafe(_lock); return;
            }
        }
        // Over the (dynamic) limit: fade out the oldest held notes to make room
        const uint8_t lim = voiceLimit;
        while (_countActive() >= lim) {
            int oldest = _findOldestActive();
            if (oldest < 0) break;
            _voices[oldest].startFade();
            _age[oldest] = 0;
        }
        int slot = _findFreeVoice();
        _voices[slot].setVibratoState(_vibQ);       // start already modulated
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

    uint8_t masterVolume = 255;   // 0–255, set via serial 'vol' or MIDI_CC_VOLUME

    // ---- Effects -------------------------------------------
    Overdrive overdrive;
    Vibrato   vibrato;
    Chorus    chorus;
    Leslie    leslie;

    // Handle effects CCs — returns true if consumed
    bool handleFxCC(uint8_t cc, uint8_t value) {
        switch (cc) {
            case MIDI_CC_DRIVE:          overdrive.drive   = value; return true;
            case MIDI_CC_CHORUS_AMOUNT:  chorus.depth = value; chorus.mix = value; return true;
            case MIDI_CC_LESLIE:         leslie.setSwitch(value);   return true;
            case MIDI_CC_VIBRATO_SWITCH:
                switch (midi_switch3(value)) {
                    case 0:  vibrato.depth = 0;                       break;
                    case 1:  vibrato.depth = VIBRATO_SWITCH_MEDIUM;   break;
                    default: vibrato.depth = VIBRATO_SWITCH_HIGH;     break;
                }
                return true;
            case MIDI_CC_VIBRATO_DEPTH:  vibrato.depth     = value; return true;
            case MIDI_CC_VIBRATO_RATE:   vibrato.rate      = value; return true;
            case MIDI_CC_CHORUS_DEPTH:   chorus.depth      = value; return true;
            case MIDI_CC_CHORUS_RATE:    chorus.rate       = value; return true;
            case MIDI_CC_CHORUS_MIX:     chorus.mix        = value; return true;
            default: return false;
        }
    }

    // ---- Audio hot path — core 1 ----------------------------
    // Stereo: the Leslie produces two "microphone" signals.
    // With the Leslie stopped both channels carry the same mono signal.
    // Force-inlined into loop1() so the whole path runs from SRAM.
    __attribute__((always_inline)) inline void tick(int16_t& left, int16_t& right) {
        // 1. Vibrato: LFO + per-voice phase increments, once per VIBRATO_BLOCK samples
        if (--_vibCount <= 0) {
            _vibCount = VIBRATO_BLOCK;
            vibrato.tick(VIBRATO_BLOCK);
            const uint32_t q = (uint32_t)(vibrato.pitchMult * (float)VIB_ONE + 0.5f);
            if (q != _vibQ) {
                _vibQ = q;
                for (int i = 0; i < MAX_TW_VOICES; i++)
                    if (_voices[i].active) _voices[i].applyVibrato(q);
            }
        }

#if ENABLE_PERF
        const uint32_t t0 = perf_now();
#endif
        // 2. Voices: unscaled sum, one scale factor for everything.
        //    Old scaling: (sine*amp >> 8) per partial, /10 per voice, /3 overall
        //    = raw / 7680.  2^32 / 7680 = 559240.5
        int32_t acc = 0;
        bool locked = spin_try_lock_unsafe(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++)
            acc += _voices[i].tickRaw();
        if (locked) spin_unlock_unsafe(_lock);
        int32_t mix = (int32_t)(((int64_t)acc * 559241) >> 32);
#if ENABLE_PERF
        const uint32_t t1 = perf_now();
        _cycVoices += t1 - t0;
#endif

        if (perc.enabled) {
            int32_t ps = perc.tick();
            if (_cachedDbSum > 0)
                mix += (ps * _cachedDbSum) / 288;
        }

        {
            int32_t cs = click.tick();
            mix += (cs * (_cachedDbSum + 18)) / 360;
        }

        // 3. Overdrive
        mix = overdrive.process(mix);
#if ENABLE_PERF
        const uint32_t t2 = perf_now();
        _cycOd += t2 - t1;
#endif

        // 4. Chorus
        mix = chorus.process(mix);
#if ENABLE_PERF
        const uint32_t t3 = perf_now();
        _cycChorus += t3 - t2;
#endif

        // 5. Leslie (mono in, stereo out)
        int32_t l, r;
        leslie.process(mix, l, r);
#if ENABLE_PERF
        _cycLeslie += perf_now() - t3;
#endif

        // Master volume: scale by 0–255
        if (masterVolume < 255) {
            l = (l * masterVolume) / 255;
            r = (r * masterVolume) / 255;
        }

        if (l >  32767) l =  32767;
        if (l < -32768) l = -32768;
        if (r >  32767) r =  32767;
        if (r < -32768) r = -32768;
        left  = (int16_t)l;
        right = (int16_t)r;
    }

    // Mono convenience wrapper (average of both channels)
    inline int16_t tick() {
        int16_t l, r;
        tick(l, r);
        return (int16_t)(((int32_t)l + (int32_t)r) / 2);
    }

    // ---- Load governor ---------------------------------------------------
    // Polyphony cap actually in force (<= MAX_ACTIVE_VOICES). Lowered by the
    // governor when the measured audio load would exceed GOV_TARGET_LOAD_PCT.
    volatile uint8_t voiceLimit = MAX_ACTIVE_VOICES;

    // Last-buffer statistics for the `perf` command / display (written by core 1)
    struct Stats {
        volatile uint32_t loadPct = 0;       // total tick() cycles / budget
        volatile uint32_t voicesPct = 0, odPct = 0, chorusPct = 0, leslieP = 0;  // % of budget
        volatile uint32_t oscillators = 0;   // sounding partials
        volatile uint32_t perOscCycles = 0;  // measured cycles per oscillator per sample (x10)
        volatile uint32_t restCycles = 0;    // non-voice cycles per sample
        volatile uint32_t partialsPerNote = 0;
    } stats;

    // Called once per audio buffer from core 1 (loop1).
    //   cycTotal: cycles spent in tick() during the buffer, budgetCy: cycles available.
    void governor(uint32_t cycTotal, uint32_t budgetCy) {
        const float N = (float)BUFFER_FRAMES;

        // sounding oscillators and held notes right now
        uint32_t nOsc = 0; uint8_t nHeld = 0;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            nOsc += _voices[i].partialCount();
            if (_voices[i].active && !_voices[i].fading) nHeld++;
        }
        uint32_t ppn = 0;
        for (int i = 0; i < NUM_DRAWBARS; i++) if (drawbars.level[i] > 0) ppn++;
        if (ppn == 0) ppn = 1;

        // measured costs, smoothed (cycles per sample)
        // Costs rise fast and fall slowly: underestimating them is the dangerous direction.
        const float cv = (float)_cycVoices;
        if (nOsc >= 4) {
            const float p = cv / (N * (float)nOsc);              // cycles per oscillator per sample
            _perOsc += (p > _perOsc ? 0.2f : 0.03f) * (p - _perOsc);
        }
        const float rest = ((float)cycTotal - cv) / N;
        _rest += (rest > _rest ? 0.2f : 0.03f) * (rest - _rest);

        // how many notes fit under the target load?
        const float target  = (float)GOV_TARGET_LOAD_PCT * 0.01f * (float)budgetCy / N;
        float cap = (target - _rest) / (_perOsc * (float)ppn);
        int icap = (cap < (float)GOV_MIN_VOICES) ? GOV_MIN_VOICES : (cap > (float)MAX_ACTIVE_VOICES ? MAX_ACTIVE_VOICES : (int)cap);

        int lim = voiceLimit;
        if (icap < lim)       { lim = icap; _upCount = 0; }                 // down: at once
        else if (icap > lim)  { if (++_upCount >= GOV_UP_BUFFERS) { lim++; _upCount = 0; } }
        else                  { _upCount = 0; }

        // safety net, independent of the model: measured load far too high
        // Proportional: shed enough notes to bring the measured load back to the target.
        const uint32_t loadPct = (uint32_t)(100.0f * (float)cycTotal / (float)budgetCy);
        if (loadPct > GOV_EMERGENCY_PCT && nHeld > 1) {
            int e = (int)((float)nHeld * (float)GOV_TARGET_LOAD_PCT / (float)loadPct);
            if (e >= (int)nHeld) e = nHeld - 1;
            if (e < 1) e = 1;
            if (e < lim) { lim = e; _upCount = 0; }
        }
        voiceLimit = (uint8_t)lim;

        // publish statistics
        const float b = 100.0f / (float)budgetCy;
        stats.loadPct = loadPct;
        stats.voicesPct = (uint32_t)(cv * b);
#if ENABLE_PERF
        stats.odPct = (uint32_t)((float)_cycOd * b);
        stats.chorusPct = (uint32_t)((float)_cycChorus * b);
        stats.leslieP = (uint32_t)((float)_cycLeslie * b);
#endif
        stats.oscillators = nOsc;
        stats.perOscCycles = (uint32_t)(_perOsc * 10.0f);
        stats.restCycles = (uint32_t)_rest;
        stats.partialsPerNote = ppn;

        _cycVoices = _cycOd = _cycChorus = _cycLeslie = 0;
    }

    // Core 0, called from loop(): fade out held notes beyond voiceLimit.
    void serviceGovernor() {
        if (_countActive() <= voiceLimit) return;
        spin_lock_unsafe_blocking(_lock);
        while (_countActive() > voiceLimit) {
            int oldest = _findOldestActive();
            if (oldest < 0) break;
            _voices[oldest].startFade();
            _age[oldest] = 0;
        }
        _activeCount = _countActive();
        spin_unlock_unsafe(_lock);
    }

    uint8_t activeCount() const { return _activeCount; }

    void allNotesOff() {
        spin_lock_unsafe_blocking(_lock);
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            _voices[i].silence();
            _age[i] = 0;
        }
        _activeCount = 0;
        spin_unlock_unsafe(_lock);
    }

    void debugPrint() const {
        drawbars.debugPrint();
        perc.debugPrint();
        click.debugPrint();
        Serial.print("  Leslie: ");
        Serial.print(leslie.mode == 0 ? "stop" : leslie.mode == 1 ? "slow" : "fast");
        Serial.print("  horn "); Serial.print(leslie.hornHz, 2);
        Serial.print(" Hz  drum "); Serial.print(leslie.drumHz, 2); Serial.println(" Hz");
        Serial.print("  Active voices: "); Serial.print(_activeCount);
        Serial.print(" / "); Serial.print(voiceLimit);
        Serial.print(" (max "); Serial.print(MAX_ACTIVE_VOICES); Serial.println(")");
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

    // vibrato block counter / last applied multiplier (core 1 only)
    int            _vibCount = 1;
    uint32_t       _vibQ     = VIB_ONE;

    // governor state (core 1 only)
    uint32_t       _cycVoices = 0, _cycOd = 0, _cycChorus = 0, _cycLeslie = 0;
    float          _perOsc   = 14.0f;     // cycles per oscillator per sample (initial guess)
    float          _rest     = 600.0f;    // non-voice cycles per sample (initial guess)
    uint32_t       _upCount  = 0;
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
        // pool exhausted (many voices fading out): reuse a fading voice, else the oldest held one
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (_voices[i].fading) { _voices[i].silence(); return i; }
        int o = _findOldestActive();
        return o < 0 ? 0 : o;
    }

    // Oldest *held* note (fading voices are excluded); -1 if none
    int _findOldestActive() {
        int oldest = -1;
        uint32_t minAge = 0xFFFFFFFFu;
        for (int i = 0; i < MAX_TW_VOICES; i++) {
            if (_voices[i].active && !_voices[i].fading && _age[i] < minAge) {
                minAge = _age[i]; oldest = i;
            }
        }
        return oldest;
    }

    uint8_t _countActive() const {
        uint8_t n = 0;
        for (int i = 0; i < MAX_TW_VOICES; i++)
            if (_voices[i].active && !_voices[i].fading) n++;   // held notes only
        return n;
    }
};