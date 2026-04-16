#pragma once
// ============================================================
//  config.h  —  Single place for all configuration
// ============================================================

// ---- Feature flags (1=on, 0=off) ---------------------------
#define ENABLE_LCD      1   // Waveshare Pico-LCD-1.14
#define ENABLE_SERIAL   1   // Serial debug + command interface

// ---- MIDI --------------------------------------------------
#define MIDI_CHANNEL    0   // 0=OMNI, 1-16=specific channel

// Drawbar CCs (standard Korg/Roland drawbar controller)
#define MIDI_CC_DRAWBAR_1   12
#define MIDI_CC_DRAWBAR_2   13
#define MIDI_CC_DRAWBAR_3   14
#define MIDI_CC_DRAWBAR_4   15
#define MIDI_CC_DRAWBAR_5   16
#define MIDI_CC_DRAWBAR_6   17
#define MIDI_CC_DRAWBAR_7   18
#define MIDI_CC_DRAWBAR_8   19
#define MIDI_CC_DRAWBAR_9   20

// Percussion CCs
#define MIDI_CC_PERC_ONOFF      80   // >=64=on
#define MIDI_CC_PERC_HARMONIC   81   // >=64=3rd
#define MIDI_CC_PERC_DECAY      82   // >=64=fast
#define MIDI_CC_PERC_LEVEL      83   // >=64=soft

// Click CC
#define MIDI_CC_CLICK           84

// Volume CC
#define MIDI_CC_VOLUME           7

// Set to a note number to toggle percussion via that note, or -1 to disable
#define MIDI_NOTE_PERC_TOGGLE   -1

// Program Change preset select (0=Full, 1=Jazz, 2=Flute, 3=Off)
#define MIDI_PROGCHANGE_PRESETS  1

// ---- Polyphony cap -----------------------------------------
// Maximum simultaneous voices before oldest is stolen.
// Raise for Pico 2, lower for more stability.
// Rule of thumb: (CPU_MHz / 133) * 8 is a safe starting point.
//   Pico  (RP2040, 133MHz): 8
//   Pico 2 (RP2350, 150MHz): 12
#define MAX_ACTIVE_VOICES   12
#define AUDIO_DATA_PIN        22
#define AUDIO_CLOCK_PIN_BASE  26   // BCK=GP26, LRCK=GP27

// ---- Audio parameters --------------------------------------
#define SAMPLE_RATE    44100
#define BUFFER_FRAMES  256

// ---- Wavetable ---------------------------------------------
#define WAVETABLE_BITS 10
#define WAVETABLE_SIZE (1 << WAVETABLE_BITS)
#define PHASE_SHIFT    (32 - WAVETABLE_BITS)

// ---- Misc --------------------------------------------------
#define LED_PIN        25