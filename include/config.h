#pragma once
#include <stdint.h>
// ============================================================
//  config.h  —  Pico 2 (RP2350) optimised build
//  Overclock: 200 MHz  |  Voices: 16  |  Wavetable: 4096 pts
// ============================================================

// ---- Feature flags -----------------------------------------
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#define ENABLE_USB_HOST   1   // USB MIDI host on PIO0/PIO1 via Pico-PIO-USB
                              // Audio moved to PIO2 to avoid conflict
                              // Requires USB-A socket wired to GP6 (D+) and GP7 (D-)
                              // with 15kΩ pull-down on GP6, and 5V on VBUS

// ---- USB host pins -----------------------------------------
// D+ on GP6, D- automatically on GP7 (DP+1, DPDM pinout)
// These GPIO must be free — check pin conflict table in README
#define MIDI_HOST_DP_PIN  6

// ---- Polyphony ---------------------------------------------
// RP2350 @ 200 MHz with HW divide + FPU handles 16 comfortably.
// Raise to 20 if you overclock to 250 MHz.
#define MAX_ACTIVE_VOICES  16

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL    0   // 0=OMNI

#define MIDI_CC_DRAWBAR_1   12
#define MIDI_CC_DRAWBAR_2   13
#define MIDI_CC_DRAWBAR_3   14
#define MIDI_CC_DRAWBAR_4   15
#define MIDI_CC_DRAWBAR_5   16
#define MIDI_CC_DRAWBAR_6   17
#define MIDI_CC_DRAWBAR_7   18
#define MIDI_CC_DRAWBAR_8   19
#define MIDI_CC_DRAWBAR_9   20

#define MIDI_CC_PERC_ONOFF      80
#define MIDI_CC_PERC_HARMONIC   81
#define MIDI_CC_PERC_DECAY      82
#define MIDI_CC_PERC_LEVEL      83
#define MIDI_CC_CLICK           84
#define MIDI_CC_VOLUME           21   // keyboard pot 1

#define MIDI_NOTE_PERC_TOGGLE   -1
#define MIDI_PROGCHANGE_PRESETS  1

// ---- I2S pins (Waveshare Pico-Audio Rev 2.1) ---------------
#define AUDIO_DATA_PIN        22
#define AUDIO_CLOCK_PIN_BASE  26

// ---- Audio -------------------------------------------------
#define SAMPLE_RATE    44100
#define BUFFER_FRAMES  256

// ---- Wavetable — 4096 points for Pico 2 -------------------
// SFDR improves from ~60 dB (1024 pts) to ~72 dB (4096 pts)
// Memory cost: 4096 × 2 = 8 KB (vs 2 KB) — trivial on 520 KB SRAM
#define WAVETABLE_BITS 12
#define WAVETABLE_SIZE (1 << WAVETABLE_BITS)   // 4096
#define PHASE_SHIFT    (32 - WAVETABLE_BITS)   // 20

// ---- Misc --------------------------------------------------
#define LED_PIN  25

// ---- Effects CC mapping ------------------------------------
// Overdrive
#define MIDI_CC_DRIVE           22   // keyboard pot 2: 0=off, 1–127=drive amount

// Vibrato
#define MIDI_CC_VIBRATO_DEPTH   86   // 0=off, 1–127=depth
#define MIDI_CC_VIBRATO_RATE    87   // 0..127=rate (0.5–9 Hz)
// Max vibrato depth in semitones at depth=127.
// Hammond vibrato is very subtle — 0.25 semitones is authentic.
// Increase for more dramatic effect (0.5 = noticeable, 1.0 = strong)
#define VIBRATO_MAX_SEMITONES   0.25f

// Chorus
#define MIDI_CC_CHORUS_DEPTH    88   // 0=off, 1–127=modulation depth
#define MIDI_CC_CHORUS_RATE     89   // 0..127=rate (0.2–3 Hz)
#define MIDI_CC_CHORUS_MIX      90   // 0..127=wet mix (0..50%)
#define CHORUS_MIX_DEFAULT      64   // default mix (~25% wet)

// ============================================================
//  Keyboard controls: 3 pots, 3 three-state switches, Leslie switch
// ============================================================
// Pots (continuous 0..127):
//   CC21 = master volume  (MIDI_CC_VOLUME above)
//   CC22 = overdrive      (MIDI_CC_DRIVE above)
#define MIDI_CC_CHORUS_AMOUNT   23   // keyboard pot 3: sets chorus depth AND mix together
                                     // (rate stays as set by CC89 / serial)

// Three-state switches send 2 / 62 / 126. Thresholds sit halfway between
// those values: <32 = position 0, <94 = position 1, otherwise position 2.
static inline uint8_t midi_switch3(uint8_t v) { return v < 32 ? 0 : (v < 94 ? 1 : 2); }

#define MIDI_CC_PERC_SWITCH     24   // 0 = off, 1 = on (2nd harmonic), 2 = on (3rd harmonic)
#define MIDI_CC_VIBRATO_SWITCH  25   // 0 = off, 1 = medium, 2 = high
#define MIDI_CC_CLICK_SWITCH    26   // 0 = off, 1 = medium, 2 = high

// Switch positions 1 and 2 → parameter values
#define VIBRATO_SWITCH_MEDIUM   64   // vibrato depth (0..127), see VIBRATO_MAX_SEMITONES
#define VIBRATO_SWITCH_HIGH     127
#define CLICK_LEVEL_MEDIUM      40   // click level (0..255 scale, same as CC84)
#define CLICK_LEVEL_HIGH        110

// ============================================================
//  Leslie (rotary speaker simulation, effects.h)
// ============================================================
#define MIDI_CC_LESLIE          27   // half-moon switch: slow / stop / fast

// What each switch position selects: 0 = stop, 1 = slow (chorale), 2 = fast (tremolo).
// Default: LOW value = slow, MIDDLE = stop, HIGH = fast.
// If the switch turns out to be wired the other way round, just swap LOW and HIGH.
#define LESLIE_POS_LOW          1
#define LESLIE_POS_MID          0
#define LESLIE_POS_HIGH         2

#define LESLIE_STEREO           1       // 0 = both outputs carry the same mono mix
#define LESLIE_CROSSOVER_HZ     800.0f  // horn (highs) / drum (lows) split

// Rotor speeds (real Leslie 122: horn 48/400 rpm, drum 40/342 rpm)
#define LESLIE_HORN_SLOW_HZ     0.8f
#define LESLIE_HORN_FAST_HZ     6.7f
#define LESLIE_DRUM_SLOW_HZ     0.67f
#define LESLIE_DRUM_FAST_HZ     5.7f

// Run-up / run-down: time constants in seconds (exponential approach to the
// new speed, ~3 time constants to get there). The drum is much heavier.
#define LESLIE_HORN_ACCEL_S     0.8f
#define LESLIE_HORN_DECEL_S     1.5f
#define LESLIE_DRUM_ACCEL_S     2.0f
#define LESLIE_DRUM_DECEL_S     3.5f

#define LESLIE_HORN_DEPTH       0.6f    // amplitude modulation depth, 0..1
#define LESLIE_DRUM_DEPTH       0.4f
#define LESLIE_HORN_DOPPLER_MS  0.5f    // horn radius as delay swing (0.5 ms ≈ ±2% pitch at fast)
#define LESLIE_MIC_OFFSET       0.25f   // right "microphone" phase offset (rotor cycles)
#define LESLIE_MAKEUP           1.25f   // output makeup gain (AM lowers the average level)
#define LESLIE_STOP_HZ          0.15f   // below this (and switch on stop) the effect fades to dry

// ============================================================
//  Load governor — keeps the audio CPU load below a safe level
// ============================================================
// Core 1 measures what each oscillator really costs and limits polyphony so
// the DAC never starves (starvation shows up as pitch drop, then silence).
// Voices over the limit fade out (VOICE_FADE_SAMPLES) instead of being cut.
#define GOV_TARGET_LOAD_PCT   80    // polyphony is capped so predicted load stays below this
#define GOV_EMERGENCY_PCT     92    // measured load above this sheds a voice at once
#define GOV_MIN_VOICES        3     // never limit below this
#define GOV_UP_BUFFERS        170   // spare capacity needed for ~1 s before allowing 1 more voice
#define VOICE_FADE_SAMPLES    220   // ~5 ms fade-out for stolen voices
#define VIBRATO_BLOCK         16    // vibrato phase increments are updated every N samples
#define ENABLE_PERF           1     // per-stage cycle counters for the `perf` serial command

// ============================================================
//  Overdrive (effects.h)
// ============================================================
#ifndef OD_MAX_GAIN_DB
#define OD_MAX_GAIN_DB    36.0f     // drive gain at CC value 127 (0 dB at CC value 1)
#endif
#ifndef OD_GAIN_CURVE
#define OD_GAIN_CURVE     1.0f      // 1.0 = gain in dB rises linearly with the drive value; >1 = back-loaded
#endif
#ifndef OD_LEVEL_DB
#define OD_LEVEL_DB       2.5f      // loudness at max drive relative to the clean signal (dB)
#endif
#define OD_LPF_HIGH_HZ    16000.0f  // post-clipper rolloff at lowest drive
#define OD_LPF_LOW_HZ     5000.0f   // ... and at max drive