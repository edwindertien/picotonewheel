#pragma once
// ============================================================
//  effects.h  —  Post-processing effects chain
//
//  Applied in TonewheelManager::tick() after voice mixing,
//  percussion and click, before master volume and final clip.
//
//  Chain order:
//    1. Overdrive  — asymmetric tube-style waveshaper
//    2. Vibrato    — sine LFO pitch modulation (all voices)
//    3. Chorus     — dual quadrature BBD-style delay line
//    4. Leslie     — rotary speaker (horn + drum), stereo output
//
//  All methods called from core 1 only. No Serial, no malloc.
//  Parameters are public — set from core 0 via CC or serial.
//  Reads/writes of uint8_t are atomic on ARM; no spinlock needed
//  for parameter updates (worst case one slightly stale sample).
// ============================================================

#include <Arduino.h>
#include "config.h"

// ---- Fast sine approximation (no table, ~10 FPU cycles) ----
// phase: 0.0 .. 1.0 (one full cycle, any value is wrapped). Returns sin(2*pi*phase).
// Parabola + one refinement step: max error ~0.001, continuous at the wrap point.
// (The previous polynomial was off by 0.54 at the ends of its range, giving a
//  step of ~1.08 in the LFO once per cycle -> audible click in chorus/vibrato.)
static inline float _fx_sin(float phase) {
    float x = phase - floorf(phase + 0.5f);   // -0.5 .. +0.5
    float t = 2.0f * x;                       // -1 .. +1
    float y = 4.0f * t * (1.0f - fabsf(t));   // parabola through sin(pi*t)
    return 0.225f * (y * fabsf(y) - y) + y;   // refinement
}

// ============================================================
//  1. OVERDRIVE  (asymmetric tube-style waveshaper)
//
//  Uses an asymmetric rational waveshaper — different curves for
//  positive and negative halves — which generates even harmonics
//  (2nd, 4th) that sound warm and tube-like. The symmetric cubic
//  shaper only generates odd harmonics (3rd, 5th) which sound
//  harsher and more transistor-like.
//
//  Positive half: y = x / (1 + x)    ← gentle, smooth rolloff
//  Negative half: y = x / (1 - 0.5x) ← slightly harder clip
//
//  Both halves stay within ±1 naturally — no hard clamping.
//  The asymmetry is intentional: real tube output stages are not
//  symmetric; this asymmetry is what gives valves their character.
//
//  drive = 0   → bypass (true bypass, zero processing)
//  drive 1–127 → input gain 1.0..5.0, followed by shaper + 1/g comp
// ============================================================
struct Overdrive {
    uint8_t drive = 0;

    inline int32_t process(int32_t x) const {
        if (drive == 0) return x;

        // Input gain: 1.0..5.0
        float g = 1.0f + (float)(drive - 1) * (4.0f / 126.0f);
        float f = (float)x * g * (1.0f / 32768.0f);  // normalise + drive

        // Asymmetric tube waveshaper — generates even harmonics
        if (f >= 0.0f)
            f = f / (1.0f + f);          // positive: smooth rolloff
        else
            f = f / (1.0f - 0.5f * f);  // negative: slightly harder

        // Level compensation: 1/g so output stays close to input level.
        // The shaper adds warmth/character without changing loudness.
        f /= g;

        return (int32_t)(f * 32768.0f);
    }
};

// ============================================================
//  2. VIBRATO
//  Sine LFO modulates pitch of all active voices each sample.
//  The frequency deviation is applied via a shared pitch_bend
//  factor that TonewheelManager reads and multiplies into
//  each voice's oscillator frequency in its tick() call.
//
//  depth = 0 → bypass
//  depth = 1..127 → ±0.006 .. ±0.75 semitones (subtle to strong)
//  rate  = 0..127 → 0.5 Hz .. 9.0 Hz LFO speed
// ============================================================
struct Vibrato {
    uint8_t depth = 0;    // 0 = off
    uint8_t rate  = 40;   // default ~4 Hz

    // Output: pitch multiplier for all voices (1.0 = no shift)
    // TonewheelManager reads this each sample from core 1.
    volatile float pitchMult = 1.0f;

    // Internal LFO state (core 1 only, no volatile needed)
    float _phase = 0.0f;

    inline void tick() {
        if (depth == 0) {
            pitchMult = 1.0f;
            return;
        }
        // Rate: 0..127 → 0.5..9.0 Hz at 44100 Hz sample rate
        float hz     = 0.5f + (float)rate * (8.5f / 127.0f);
        float inc    = hz / (float)SAMPLE_RATE;
        _phase      += inc;
        if (_phase >= 1.0f) _phase -= 1.0f;

        float lfo    = _fx_sin(_phase);  // -1..+1

        // Depth: 0..127 → 0..VIBRATO_MAX_SEMITONES (from config.h)
        // Default 0.25 semitones is authentic Hammond vibrato.
        // Increase VIBRATO_MAX_SEMITONES in config.h for more effect.
        float semis  = (float)depth * (VIBRATO_MAX_SEMITONES / 127.0f);
        float devHz  = semis * 0.05946f;   // semitone ratio (2^(1/12)-1 ≈ 0.05946)
        pitchMult    = 1.0f + lfo * devHz;
    }
};

// ============================================================
//  3. CHORUS
//  Dual quadrature BBD-style chorus (Boss CE-2 architecture).
//
//  Two delay lines driven by LFOs 90° apart. When one LFO is
//  at its turnaround point (fastest rate of change → most
//  audible artifact), the other is at its flattest — the two
//  lines mask each other's modulation reversals, giving a
//  smooth, natural chorus character with no periodic click.
//
//  depth = 0   → bypass
//  depth 1–127 → modulation swing (0..2ms, ±~20 cents max)
//  rate  0–127 → LFO speed (0.2..3.0 Hz)
//  mix   0–127 → wet/dry mix (0..50% wet)
//
//  Two delay buffers: 2 × 2048 × 2 bytes = 8 KB SRAM
// ============================================================
struct Chorus {
    uint8_t depth = 0;                     // 0 = bypass
    uint8_t rate  = 30;                    // ~0.7 Hz default
    uint8_t mix   = CHORUS_MIX_DEFAULT;   // wet level

    static constexpr int BUF = 2048;
    int16_t _buf0[BUF] = {};   // delay line A (LFO phase 0°)
    int16_t _buf1[BUF] = {};   // delay line B (LFO phase 90°)
    int     _write = 0;
    float   _phase = 0.0f;

    inline int32_t process(int32_t x) {
        // Clamp and write to both delay lines
        int16_t xs = (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
        _buf0[_write & (BUF-1)] = xs;
        _buf1[_write & (BUF-1)] = xs;

        if (depth == 0) {
            _write++;
            return x;
        }

        // LFO: advance phase
        float hz  = 0.2f + (float)rate * (2.8f / 127.0f);
        _phase   += hz / (float)SAMPLE_RATE;
        if (_phase >= 1.0f) _phase -= 1.0f;

        // Quadrature LFOs: 0° and 90°
        float lfo0 = _fx_sin(_phase);           // sin
        float lfo1 = _fx_sin(_phase + 0.25f);   // cos (90° ahead)

        // Modulation depth: 0..2ms swing (±1ms at depth=127)
        float centreMs = 20.0f;
        float modulMs  = (float)depth * (2.0f / 127.0f);

        // Delay line A
        float dA     = (centreMs + lfo0 * modulMs) * (SAMPLE_RATE / 1000.0f);
        int   dA0    = (int)dA;
        float fracA  = dA - (float)dA0;
        float wetA   = _buf0[(_write - dA0)     & (BUF-1)]
                     + fracA * (float)(_buf0[(_write - dA0 - 1) & (BUF-1)]
                                     - _buf0[(_write - dA0)     & (BUF-1)]);

        // Delay line B (90° offset)
        float dB     = (centreMs + lfo1 * modulMs) * (SAMPLE_RATE / 1000.0f);
        int   dB0    = (int)dB;
        float fracB  = dB - (float)dB0;
        float wetB   = _buf1[(_write - dB0)     & (BUF-1)]
                     + fracB * (float)(_buf1[(_write - dB0 - 1) & (BUF-1)]
                                     - _buf1[(_write - dB0)     & (BUF-1)]);

        _write++;

        // Average both wet signals — their artifacts cancel
        float wet    = (wetA + wetB) * 0.5f;

        // Mix: 0..50% wet (mix=127 → 50%)
        float wetMix = (float)mix * (0.5f / 127.0f);
        return (int32_t)((float)x * (1.0f - wetMix) + wet * wetMix);
    }
};

// ============================================================
//  4. LESLIE  (rotary speaker simulation, stereo out)
//
//  A real Leslie has two rotors: a horn (highs) and a drum (lows),
//  each turning at its own speed. The sound at a fixed microphone
//  is modulated in two ways:
//    - amplitude: the speaker beam sweeps past the microphone
//    - pitch (Doppler): the horn mouth moves towards / away from it
//
//  Model:
//    input -> crossover (LESLIE_CROSSOVER_HZ, 12 dB/oct each side, sums flat)
//      high band -> horn: short modulated delay (Doppler) + amplitude mod
//      low band  -> drum: amplitude mod only (the drum's Doppler is negligible)
//    Two "microphones" a quarter turn apart give the left / right outputs.
//
//  The low band is delayed by the horn's mean delay so the two bands stay
//  aligned around the crossover (otherwise they partly cancel there).
//
//  Speed control: mode 0 = stop, 1 = slow (chorale), 2 = fast (tremolo).
//  The rotor speeds approach their targets exponentially, with separate
//  run-up / run-down time constants for horn and drum (config.h), so
//  switching sweeps smoothly between speeds. After a stop the speeds fade
//  below LESLIE_STOP_HZ, the effect is crossfaded to dry and then bypassed
//  completely (no CPU, no colouration).
//
//  Cost: ~4 sine evaluations + a handful of multiplies per sample.
//  Memory: 2 x 128 floats. Core 1 only; `mode` is written from core 0.
// ============================================================
struct Leslie {
    volatile uint8_t mode = 0;   // 0 = stop, 1 = slow, 2 = fast

    // Current rotor speeds in Hz (for display / debug; written by core 1)
    float hornHz = 0.0f;
    float drumHz = 0.0f;

    // Map the CC27 value (three-position switch) onto a mode
    void setSwitch(uint8_t ccValue) {
        switch (midi_switch3(ccValue)) {
            case 0:  mode = LESLIE_POS_LOW;  break;
            case 1:  mode = LESLIE_POS_MID;  break;
            default: mode = LESLIE_POS_HIGH; break;
        }
    }

    Leslie() {
        const float fs = (float)SAMPLE_RATE;
        const float g = tanf(3.14159265f * LESLIE_CROSSOVER_HZ / fs);
        _xoG      = g / (1.0f + g);
        _hUp      = 1.0f / (LESLIE_HORN_ACCEL_S * fs);
        _hDn      = 1.0f / (LESLIE_HORN_DECEL_S * fs);
        _dUp      = 1.0f / (LESLIE_DRUM_ACCEL_S * fs);
        _dDn      = 1.0f / (LESLIE_DRUM_DECEL_S * fs);
        _wetSlew  = 1.0f / (0.4f * fs);
        _dAmp     = LESLIE_HORN_DOPPLER_MS * fs / 1000.0f;   // samples
        _dBase    = _dAmp + 1.0f;                            // keeps delay >= 1 sample
        _lowDelay = (int)(_dBase + 0.5f);
        _hg0      = 1.0f - 0.5f * LESLIE_HORN_DEPTH;   // gain = _g0 + _g1 * cos(angle)
        _hg1      =        0.5f * LESLIE_HORN_DEPTH;
        _dg0      = 1.0f - 0.5f * LESLIE_DRUM_DEPTH;
        _dg1      =        0.5f * LESLIE_DRUM_DEPTH;
        _drumPh   = 0.37f;   // start the rotors out of step
    }

    // in: mono sample (int16 range, int32 headroom). Returns stereo via outL/outR.
    inline void process(int32_t in, int32_t& outL, int32_t& outR) {
        // ---- rotor speeds: exponential approach (rotor inertia) -------
        const uint8_t m = mode;
        const float hT = (m == 1) ? LESLIE_HORN_SLOW_HZ : (m == 2) ? LESLIE_HORN_FAST_HZ : 0.0f;
        const float dT = (m == 1) ? LESLIE_DRUM_SLOW_HZ : (m == 2) ? LESLIE_DRUM_FAST_HZ : 0.0f;
        hornHz += (hT - hornHz) * (hT > hornHz ? _hUp : _hDn);
        drumHz += (dT - drumHz) * (dT > drumHz ? _dUp : _dDn);

        // ---- stopped and wound down: fade to dry, then bypass ----------
        const bool spinning = (m != 0) || hornHz > LESLIE_STOP_HZ || drumHz > LESLIE_STOP_HZ;
        _wet += ((spinning ? 1.0f : 0.0f) - _wet) * _wetSlew;
        if (!spinning && _wet < 0.001f) {
            _wet = 0.0f; hornHz = 0.0f; drumHz = 0.0f;
            outL = outR = in;
            return;
        }

        // ---- crossover: 12 dB/oct on both sides ---------------------------
        // low  = H(H(x)),  high = -(1-H)(1-H)(x)  with H a one-pole lowpass.
        // The high band is polarity-inverted so low + high is an allpass, i.e.
        // flat. H uses the bilinear (TPT) form so that 1-H is an exact highpass
        // and the sum is exactly flat. (A plain "high = x - low" would only be
        // 6 dB/oct and let bass notes leak into the horn.)
        const float x = (float)in;
        float v;
        v = (x - _s1) * _xoG;  const float lp1 = v + _s1;  _s1 = lp1 + v;     // H x
        v = (lp1 - _s2) * _xoG; const float lp2 = v + _s2; _s2 = lp2 + v;     // H H x
        const float h1 = x - lp1;                                              // (1-H) x
        v = (h1 - _s3) * _xoG;  const float lp3 = v + _s3;  _s3 = lp3 + v;    // H (1-H) x
        const float low  = lp2;
        const float high = lp3 - h1;                                           // -(1-H)(1-H) x

        _hbuf[_w & MASK] = high;
        _lbuf[_w & MASK] = low;

        // ---- rotor angles ----------------------------------------------
        _hornPh += hornHz * (1.0f / (float)SAMPLE_RATE);
        if (_hornPh >= 1.0f) _hornPh -= 1.0f;
        _drumPh += drumHz * (1.0f / (float)SAMPLE_RATE);
        if (_drumPh >= 1.0f) _drumPh -= 1.0f;

        // cos(angle) as seen by the left and right microphone
        const float cHL = _fx_sin(_hornPh + 0.25f);
        const float cHR = _fx_sin(_hornPh + LESLIE_MIC_OFFSET + 0.25f);
        const float cDL = _fx_sin(_drumPh + 0.25f);
        const float cDR = _fx_sin(_drumPh + LESLIE_MIC_OFFSET + 0.25f);

        // ---- horn: Doppler delay + amplitude ---------------------------
        const float hL = _tap(_hbuf, _dBase - _dAmp * cHL) * (_hg0 + _hg1 * cHL);
        const float hR = _tap(_hbuf, _dBase - _dAmp * cHR) * (_hg0 + _hg1 * cHR);

        // ---- drum: amplitude only (delay-aligned with the horn) ---------
        const float lo = _lbuf[(_w - _lowDelay) & MASK];
        const float dL = lo * (_dg0 + _dg1 * cDL);
        const float dR = lo * (_dg0 + _dg1 * cDR);
        _w++;

        float L = (hL + dL) * LESLIE_MAKEUP;
        float R = (hR + dR) * LESLIE_MAKEUP;
#if !LESLIE_STEREO
        L = R = 0.5f * (L + R);
#endif
        const float dry = x * (1.0f - _wet);
        outL = (int32_t)(dry + L * _wet);
        outR = (int32_t)(dry + R * _wet);
    }

private:
    static constexpr int BUF  = 128;
    static constexpr int MASK = BUF - 1;
    static_assert(2.0f * LESLIE_HORN_DOPPLER_MS * SAMPLE_RATE / 1000.0f + 3.0f < BUF,
                  "LESLIE_HORN_DOPPLER_MS too large for the delay buffer");

    float _hbuf[BUF] = {};
    float _lbuf[BUF] = {};
    int   _w = 0;

    float _s1 = 0.0f, _s2 = 0.0f, _s3 = 0.0f;   // crossover filter states
    float _hornPh = 0.0f, _drumPh = 0.0f;
    float _wet = 0.0f;

    float _xoG, _hUp, _hDn, _dUp, _dDn, _wetSlew;
    float _dAmp, _dBase, _hg0, _hg1, _dg0, _dg1;
    int   _lowDelay;

    // Linear-interpolated read, d samples behind the newest sample
    inline float _tap(const float* b, float d) const {
        const int   i = (int)d;
        const float f = d - (float)i;
        const float s0 = b[(_w - i)     & MASK];
        const float s1 = b[(_w - i - 1) & MASK];
        return s0 + f * (s1 - s0);
    }
};