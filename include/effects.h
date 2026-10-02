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
//  All process()/tick() methods are force-inlined: they end up inside loop1(),
//  which lives in SRAM. (Out-of-line they would execute from flash through the
//  XIP cache, which core 0 constantly evicts -> unpredictable stalls on core 1.)
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
__attribute__((always_inline)) static inline float _fx_sin(float phase) {
    // phase must be >= -0.5 (all callers pass 0..~1.5): truncation == floor for x >= 0
    float x = phase - (float)(int)(phase + 0.5f);   // -0.5 .. +0.5
    float t = 2.0f * x;                             // -1 .. +1
    float y = 4.0f * t * (1.0f - fabsf(t));         // parabola through sin(pi*t)
    return 0.225f * (y * fabsf(y) - y) + y;         // refinement
}

// ============================================================
//  1. OVERDRIVE  (asymmetric tube-style clipper, anti-aliased, level-following)
//
//  Signal path:
//    in -> split at ~120 Hz:  bass stays clean (no muddy low-frequency
//          intermodulation), the rest is distorted
//       -> drive gain (0 .. +34 dB, exponential)
//       -> 2x oversampling (15-tap half-band, polyphase)
//       -> asymmetric soft clipper with first-order antiderivative
//          anti-aliasing (ADAA): even + odd harmonics, no hard knee
//       -> half-band decimation
//       -> 2 x one-pole lowpass (16 kHz at low drive .. 5 kHz at max: amp/cab
//          rolloff, also mops up residual alias products)
//       -> DC blocker (the asymmetry creates a signal-dependent DC offset)
//       -> loudness-following makeup: the clipped output has a fixed ceiling,
//          so its level is tied to the INPUT level (fast attack 20 ms /
//          release 5 ms detectors) -> loudness stays close to the clean signal
//          (+0..+2 dB at max drive) for one note or a six-note chord alike
//       -> + clean bass (delayed by the oversampler latency, 7 samples)
//
//  Why so much machinery: a plain waveshaper at this drive aliases at about
//  -25 dB (inharmonic hash that sounds like noise). ADAA + 2x oversampling
//  brings it to about -55..-60 dB (ear-weighted, at max drive). Cost: roughly
//  250-300 cycles/sample (~5% of the audio budget), paid only while drive > 0.
//  No libm calls in the audio path (parameters come from a 128-entry table
//  built at start-up). The makeup calibration below was measured with
//  OD_MAX_GAIN_DB = 36 and OD_GAIN_CURVE = 1.0.
//
//  drive = 0   -> bypass (crossfaded, so switching never clicks)
//  drive 1..127
// ============================================================
struct Overdrive {
    uint8_t drive = 0;

    Overdrive() {
        const float fs = (float)SAMPLE_RATE;
        for (int d = 1; d < 128; d++) {
            const float t   = (float)(d - 1) / 126.0f;
            const float gdb = OD_MAX_GAIN_DB * powf(t, OD_GAIN_CURVE);
            const float s   = powf((float)d / 127.0f, 1.3f);
            // Makeup calibration (measured on 5 organ test signals, fit error <= 0.25 dB):
            // with 0 dB makeup the chain's level vs clean is  0.089 - 1.163*s  (dB).
            // Cancel that and add the target, so OD_LEVEL_DB is the loudness at max drive
            // and the level rises smoothly from 0 dB at the bottom of the pot.
            // (Re-measure if the clipper, filters or OD_GAIN_CURVE are changed.)
            const float kdb = (OD_LEVEL_DB + 1.163f) * s - 0.089f;
            const float fc  = OD_LPF_HIGH_HZ * powf(OD_LPF_LOW_HZ / OD_LPF_HIGH_HZ, t);
            _tab[d].g   = powf(10.0f, gdb * 0.05f);
            _tab[d].k   = powf(10.0f, kdb * 0.05f);
            _tab[d].lpA = 1.0f - expf(-6.2831853f * fc / fs);
        }
        _tab[0] = _tab[1];
        _aLo     = 1.0f - expf(-6.2831853f * 120.0f / fs);
        _atk     = 1.0f - expf(-1.0f / (0.020f * fs));
        _rel     = 1.0f - expf(-1.0f / (0.005f * fs));
        _wetSlew = 1.0f / (0.010f * fs);
        _reset();
    }

    __attribute__((always_inline)) inline int32_t process(int32_t in) {
        // ---- on/off crossfade (the wet path has 7 samples of latency) -------
        const bool on = (drive != 0);
        _wet += ((on ? 1.0f : 0.0f) - _wet) * _wetSlew;
        if (!on && _wet < 0.001f) { _wet = 0.0f; _idle = true; return in; }
        if (_idle) { _reset(); _idle = false; }
        if (on) _last = drive;
        const Tab& p = _tab[_last];

        const float x = (float)in * (1.0f / 32768.0f);

        // ---- split: clean bass / distorted rest -----------------------------
        _lo += _aLo * (x - _lo);
        const float hi = x - _lo;
        // Delay lines are "doubled" rings: every sample is stored twice (i and i+N),
        // so any window of the last N samples is contiguous and every tap is a plain
        // load with a constant offset (no index masking in the inner loops).
        _li = (_li + 1) & 7;
        _lb[_li] = _lo; _lb[_li + 8] = _lo;
        const float loD = _lb[_li + 1];                 // lo from 7 samples ago

        // ---- 2x oversampling (half-band, polyphase) ---------------------------
        _ui = (_ui + 1) & 7;
        const float uin = hi * p.g;
        _uh[_ui] = uin; _uh[_ui + 8] = uin;
        const float* U = &_uh[_ui + 8];                 // U[-j] = input j samples ago
        const float ze = U[-4];
        const float zo = 2.0f * (HB1 * (U[-4] + U[-3]) + HB3 * (U[-5] + U[-2])
                               + HB5 * (U[-6] + U[-1]) + HB7 * (U[-7] + U[0]));

        // ---- clipper with ADAA, at the oversampled rate -------------------------
        const float ye = _adaa(ze);
        const float yo = _adaa(zo);

        // ---- 2x decimation -----------------------------------------------------
        _yi = (_yi + 2) & 15;
        _yh[_yi] = ye;      _yh[_yi + 16] = ye;
        _yh[_yi + 1] = yo;  _yh[_yi + 17] = yo;
        const float* Y = &_yh[_yi + 17];                // Y[-j] = oversampled output j samples ago
        const float w = 0.5f * Y[-7] + HB1 * (Y[-6] + Y[-8]) + HB3 * (Y[-4] + Y[-10])
                      + HB5 * (Y[-2] + Y[-12]) + HB7 * (Y[0] + Y[-14]);

        // ---- amp / cabinet rolloff, DC block ----------------------------------------
        _p1 += p.lpA * (w - _p1);
        _p2 += p.lpA * (_p1 - _p2);
        const float yd = _p2 - _dcx + 0.9986f * _dcy;
        _dcx = _p2; _dcy = yd;

        // ---- loudness-following makeup ------------------------------------------------
        // Detectors and gain are updated every sample (a sqrt + divide, ~30 cycles).
        // Updating the gain less often was measured: it creates sample-and-hold
        // images of the gain ripple, i.e. inharmonic hash (up to 7 dB worse aliasing).
        const float px = hi * hi, py = yd * yd;
        _ex += (px > _ex ? _atk : _rel) * (px - _ex);
        _ey += (py > _ey ? _atk : _rel) * (py - _ey);
        const float m = p.k * sqrtf((_ex + 1e-9f) / (_ey + 1e-9f));
        const float out = yd * m + loD;

        return (int32_t)((x * (1.0f - _wet) + out * _wet) * 32768.0f);
    }

private:
    // 15-tap half-band (fp = 0.2 * fs_os); centre tap 0.5, even taps 0
    static constexpr float HB1 =  0.313925991f;
    static constexpr float HB3 = -0.093403660f;
    static constexpr float HB5 =  0.044110376f;
    static constexpr float HB7 = -0.026487629f;

    struct Tab { float g, k, lpA; };
    Tab   _tab[128];
    float _aLo, _atk, _rel, _wetSlew;

    float _lb[16], _uh[16], _yh[32];                // doubled rings (see process())
    int   _li = 0, _ui = 0, _yi = 0;
    float _lo, _up, _Fp, _p1, _p2, _dcx, _dcy, _ex, _ey;
    float _wet = 0.0f;
    uint8_t _last = 1;
    bool  _idle = true;

    // Asymmetric soft clipper: pos half  y = c(u),  neg half  y = K*c(u/K)
    // with c(u) = u - u^3/3 for |u|<=1, +-2/3 beyond (C1-continuous, slope 1 at 0).
    // F is its antiderivative; ADAA output = (F(u) - F(u_prev)) / (u - u_prev).
    static constexpr float AK = 0.6f;
    __attribute__((always_inline)) static inline float _c(float u) {
        return u > 1.0f ? 0.6666667f : (u < -1.0f ? -0.6666667f : u - u * u * u * (1.0f / 3.0f));
    }
    __attribute__((always_inline)) static inline float _cF(float u) {
        const float a = fabsf(u);
        return a <= 1.0f ? 0.5f * u * u - u * u * u * u * (1.0f / 12.0f) : 0.6666667f * a - 0.25f;
    }
    __attribute__((always_inline)) inline float _adaa(float u) {
        const float F  = u >= 0.0f ? _cF(u) : (AK * AK) * _cF(u * (1.0f / AK));
        const float du = u - _up;
        float y;
        if (fabsf(du) > 2e-3f) {
            y = (F - _Fp) / du;
        } else {                                          // tiny step: midpoint evaluation
            const float um = 0.5f * (u + _up);
            y = um >= 0.0f ? _c(um) : AK * _c(um * (1.0f / AK));
        }
        _up = u; _Fp = F;
        return y;
    }

    void _reset() {
        for (int i = 0; i < 16; i++) { _lb[i] = 0.0f; _uh[i] = 0.0f; }
        for (int i = 0; i < 32; i++) _yh[i] = 0.0f;
        _lo = _up = _Fp = _p1 = _p2 = _dcx = _dcy = _ex = _ey = 0.0f;
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

    // Advance the LFO by n samples (called once per VIBRATO_BLOCK samples)
    inline void tick(int n = 1) {
        if (depth == 0) {
            pitchMult = 1.0f;
            return;
        }
        // Rate: 0..127 → 0.5..9.0 Hz at 44100 Hz sample rate
        float hz     = 0.5f + (float)rate * (8.5f / 127.0f);
        float inc    = hz / (float)SAMPLE_RATE;
        _phase      += inc * (float)n;
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

    __attribute__((always_inline)) inline int32_t process(int32_t x) {
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
    __attribute__((always_inline)) inline void process(int32_t in, int32_t& outL, int32_t& outR) {
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