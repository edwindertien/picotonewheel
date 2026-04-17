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

#define MIDI_CC_DRAWBAR_1   70
#define MIDI_CC_DRAWBAR_2   71
#define MIDI_CC_DRAWBAR_3   72
#define MIDI_CC_DRAWBAR_4   73
#define MIDI_CC_DRAWBAR_5   74
#define MIDI_CC_DRAWBAR_6   75
#define MIDI_CC_DRAWBAR_7   76
#define MIDI_CC_DRAWBAR_8   77
#define MIDI_CC_DRAWBAR_9   78

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


// #pragma once
// // ============================================================
// //  config.h  —  Pico 2 (RP2350) optimised build
// //  Overclock: 200 MHz  |  Voices: 16  |  Wavetable: 4096 pts
// // ============================================================

// // ---- Feature flags -----------------------------------------
// #define ENABLE_LCD      1
// #define ENABLE_SERIAL   1

// // ---- Polyphony ---------------------------------------------
// // RP2350 @ 200 MHz with HW divide + FPU handles 16 comfortably.
// // Raise to 20 if you overclock to 250 MHz.
// #define MAX_ACTIVE_VOICES  16

// // ---- MIDI --------------------------------------------------
// #define MIDI_CHANNEL    0   // 0=OMNI

// #define MIDI_CC_DRAWBAR_1   70
// #define MIDI_CC_DRAWBAR_2   71
// #define MIDI_CC_DRAWBAR_3   72
// #define MIDI_CC_DRAWBAR_4   73
// #define MIDI_CC_DRAWBAR_5   74
// #define MIDI_CC_DRAWBAR_6   75
// #define MIDI_CC_DRAWBAR_7   76
// #define MIDI_CC_DRAWBAR_8   77
// #define MIDI_CC_DRAWBAR_9   78

// #define MIDI_CC_PERC_ONOFF      80
// #define MIDI_CC_PERC_HARMONIC   81
// #define MIDI_CC_PERC_DECAY      82
// #define MIDI_CC_PERC_LEVEL      83
// #define MIDI_CC_CLICK           84
// #define MIDI_CC_VOLUME           7

// #define MIDI_NOTE_PERC_TOGGLE   -1
// #define MIDI_PROGCHANGE_PRESETS  1

// // ---- I2S pins (Waveshare Pico-Audio Rev 2.1) ---------------
// #define AUDIO_DATA_PIN        22
// #define AUDIO_CLOCK_PIN_BASE  26

// // ---- Audio -------------------------------------------------
// #define SAMPLE_RATE    44100
// #define BUFFER_FRAMES  256

// // ---- Wavetable — 4096 points for Pico 2 -------------------
// // SFDR improves from ~60 dB (1024 pts) to ~72 dB (4096 pts)
// // Memory cost: 4096 × 2 = 8 KB (vs 2 KB) — trivial on 520 KB SRAM
// #define WAVETABLE_BITS 12
// #define WAVETABLE_SIZE (1 << WAVETABLE_BITS)   // 4096
// #define PHASE_SHIFT    (32 - WAVETABLE_BITS)   // 20

// // ---- Misc --------------------------------------------------
// #define LED_PIN  25