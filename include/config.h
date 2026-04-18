#pragma once
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
#define MIDI_CC_VOLUME           7

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
#define MIDI_CC_DRIVE           85   // 0=off, 1–127=drive amount

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