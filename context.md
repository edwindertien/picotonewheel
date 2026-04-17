# Project Context — Pico Tonewheel Organ

This file captures the full development context of this project so it can be
forked and continued in a new conversation, or used as the starting point for
a different sound module on the same hardware platform.

---

## What this project is

A dual-Pico Hammond tonewheel organ emulator. This repository contains the
**tone generator** board only. A second Pico (not yet implemented) will scan
a physical keyboard matrix and send MIDI to this board.

The tone generator runs on a Raspberry Pi Pico 2 (RP2350) stacked with:
- Waveshare Pico-Audio Rev 2.1 (CS4344 DAC, I2S)
- Waveshare Pico-LCD-1.14 (ST7789V, 240×135, SPI)

---

## Hardware platform — reusable for any sound module

The following files are **hardware drivers** with no organ-specific logic.
They can be reused as-is for any synthesiser project on this stack:

| File | What it provides |
|------|-----------------|
| `audio_driver.h` | PIO I2S driver for CS4344. `audio_driver_init()`, `audio_driver_put(left, right)` |
| `audio_pio.pio` + `audio_pio.pio.h` | Waveshare PIO I2S program (pre-assembled) |
| `midi_handler.h` | USB-MIDI + UART MIDI parser. Calls `midi_note_on/off/cc/bend/program_change()` |
| `lcd.h` / `lcd.cpp` | ST7789V display driver + full 64KB framebuffer + joystick/button handler |
| `wavetable.h` | Runtime sine LUT generator (`wavetable_init()`, `sineTable[]`) |
| `oscillator.h` | Phase-accumulator oscillator. `setFrequency()`, `setAmplitude()`, `tick()` → int16 |

The `oscillator.h` is the fundamental building block. Everything else in the
synth engine is built from it.

### Key hardware facts

- I2S: GP22=DIN, GP26=BCK, GP27=LRCK — **do not use earlephilhower I2S lib**,
  pin order is incompatible. Use the PIO driver as-is.
- LCD: GP8=DC, GP9=CS, GP10=SCK, GP11=MOSI, GP12=RST, GP13=BL
- LCD buttons: GP2=UP, GP3=PRESS, GP15=A, GP16=LEFT, GP17=B, GP18=DOWN, GP20=RIGHT
- MIDI UART RX: GP5 (31250 baud, UART1/Serial2)
- ST7789 RAM offset: col+40, row+53 — must be applied in every window-set call
- ST7789 requires display inversion ON (0x21) during init

### platformio.ini template

```ini
[common]
platform         = https://github.com/maxgerhardt/platform-raspberrypi.git
framework        = arduino
board_build.core = earlephilhower
lib_deps =
    https://github.com/adafruit/Adafruit_TinyUSB_Arduino.git
    https://github.com/FortySevenEffects/arduino_midi_library.git
build_flags = -O2 -DUSE_TINYUSB -DCFG_TUD_MIDI=1
monitor_speed = 115200

[env:pico2_tone]
extends = common
board   = rpipico2
board_build.f_cpu = 200000000L   ; 200 MHz — safe on RP2350

[env:pico_tone]
extends = common
board   = rpipico
```

---

## Organ-specific architecture

### Synthesis chain

```
noteOn(note)
  └─ TonewheelVoice::noteOn()
       └─ 9× Oscillator::setFrequency(note_freq × drawbar_mult[i])
            └─ Oscillator::setAmplitude(drawbar_level[i] / 8.0)

loop1() → TonewheelManager::tick() → int16_t sample
  ├─ 16× TonewheelVoice::tick()
  │    └─ 9× Oscillator::tick() → sum → /3 headroom
  ├─ Percussion::tick()    → float envelope × oscillator
  ├─ Click::tick()         → noise × float envelope
  └─ masterVolume scale    → soft clip → int16
```

### Dual-core synchronisation pattern

Critical lesson learned: **`spin_lock_blocking()` disables interrupts and
breaks USB-MIDI under chord bursts.** Always use:

- `spin_lock_unsafe_blocking()` on core 0 (noteOn/noteOff/CC) — leaves IRQs on
- `spin_try_lock_unsafe()` on core 1 (tick) — non-blocking, never stalls audio
- `spin_unlock_unsafe()` for both

The CS4344 has hardware automute triggered by LRCK irregularity. If core 1
ever stalls (e.g. waiting on a blocking lock), the PIO FIFO drains, LRCK
stops, and the DAC mutes. Non-blocking try-lock prevents this entirely.

### Performance (Pico 2 @ 200 MHz)

- RP2350 has hardware FPU (float = 1 cycle) and hardware divide (/ = 1 cycle)
- `loop1()` marked `__not_in_flash_func` — runs from SRAM, no XIP cache misses
- 16 voices × 9 oscillators = 144 oscillators per sample
- Each oscillator: phase add + table lookup + integer multiply = ~8 cycles
- Total per sample: ~1400 cycles / 200M Hz = 7 µs << 22.7 µs budget
- Comfortable headroom for effects, filters, or more voices

### config.h pattern

All user-facing configuration in one file. Every other file reads from it.
Pattern: define the value in `config.h`, `#include "config.h"` in every
header that uses it. No hardcoded magic numbers anywhere else.

---

## What was NOT built (next steps)

### Keyboard matrix scanner (second Pico)
The second Pico will scan a physical organ keyboard matrix and send MIDI
to the tone generator over UART (GP5) at 31250 baud. The UART parser in
`midi_handler.h` is already implemented and waiting. The scanner Pico needs:
- Matrix scan (diode-isolated rows/columns)
- Debounce
- Note-on/off with MIDI velocity (from key travel time if dual-contact keys)
- UART MIDI output

### Effects not yet implemented
- **Leslie / rotary speaker** — periodic LFO on frequency + amplitude, two
  rates (chorale slow, tremolo fast), ramp-up/ramp-down between speeds
- **Vibrato / chorus** — Hammond V/C tabs. Vibrato: pitch LFO only.
  Chorus: pitch LFO + slightly detuned copy mixed in.
- **Reverb** — would need a convolution or algorithmic reverb; probably best
  done externally given CPU budget

---

## Forking for a different sound module

To build a different synthesiser on this same hardware, keep:
- `audio_driver.h`, `audio_pio.pio`, `audio_pio.pio.h`
- `midi_handler.h`
- `oscillator.h`, `wavetable.h`
- `lcd.h`, `lcd.cpp` (adapt `_draw_top`, `_draw_bars`, `_draw_status` for your UI)
- `platformio.ini`

Replace:
- `tonewheel_voice.h` → your voice architecture
- `tonewheel_manager.h` → your polyphonic manager
- `drawbars.h`, `percussion.h`, `click.h` → your parameters
- `config.h` → your MIDI mapping and settings
- `main.cpp` → your MIDI callbacks

The `Oscillator` class is general-purpose and reusable as-is for any
wavetable synthesis. It can be used for sine, saw, square, or any waveform
stored in `sineTable[]` — just replace the wavetable content.

---

## Analog synth emulator — design sketch

For a follow-up project building a subtractive analog synth emulator on the
same hardware, here is what the architecture would look like:

### Voice architecture (one polyphonic voice)

```
Oscillator 1  (VCO1): wavetable — saw / square / tri / sine, detune
Oscillator 2  (VCO2): wavetable — same waveforms, ±24 semitone offset, detune
Sub oscillator:        VCO1 one octave down, square wave only
Noise generator:       LCG white noise (already in click.h as a pattern)
  │
  ├─ Mix (VCO1 level, VCO2 level, sub level, noise level)
  │
Filter (VCF): 2-pole or 4-pole low-pass, state-variable or ladder model
  ├─ Cutoff frequency (0–20 kHz)
  ├─ Resonance (0–1, self-oscillation at 1.0)
  ├─ Filter envelope amount
  └─ Keyboard tracking (cutoff tracks note frequency)
  │
Amplifier (VCA): envelope-controlled gain
  │
Output
```

### Envelopes
Each voice needs at minimum two ADSR envelopes: one for the VCA, one for the
VCF cutoff. With RP2350 FPU, float ADSR is free.

```cpp
struct ADSR {
    float attack, decay, sustain, release;  // all in seconds / level
    float env = 0.0f;
    enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE } state = IDLE;
    inline float tick();   // called every sample — ~10 cycles with FPU
};
```

### Filter implementation
The most important choice. Options in order of CPU cost:

1. **1-pole RC (6 dB/oct)** — 2 multiplies per sample, sounds thin
2. **2-pole state variable (SVF, 12 dB/oct)** — ~15 cycles, good for most uses,
   gives LP/BP/HP simultaneously
3. **4-pole Moog ladder (24 dB/oct)** — ~40 cycles, the classic analog sound,
   self-oscillation at high resonance

For a true analog emulator the Moog ladder is the right choice. At 200 MHz
with 8 voices: 8 × 40 = 320 cycles/sample out of 4500 available. Very
comfortable.

Moog ladder in fixed-point integer for Pico 1 (no FPU):
```cpp
// Simplified Moog ladder — Huovilainen model
// Input x, cutoff wc (0..1), resonance k (0..4)
// State: y[4] (4 filter stages)
float g  = wc * 0.9892f;
float gg = g * g;
float feedback = k * (y[3] - x);  // with nonlinearity: tanh(y[3] - x)
x = x - feedback;
y[0] = x * g + y[0] * (1.0f - g);
y[1] = y[0] * g + y[1] * (1.0f - g);
y[2] = y[1] * g + y[2] * (1.0f - g);
y[3] = y[2] * g + y[3] * (1.0f - g);
output = y[3];
```

### LFOs
One or two LFOs per voice for vibrato, filter wobble, PWM:
- Same `Oscillator` class, just running at 0.1–20 Hz instead of audio rate
- Waveforms: sine, triangle, square, sample-and-hold (random)
- Sync to note-on or free-running

### Polyphony
With 8 voices × (2 VCOs + filter + 2 envelopes + 1 LFO) per voice:
- ~200 cycles per voice at 200 MHz on RP2350
- 8 voices = 1600 cycles / 4500 budget = 36% — very comfortable
- 12 voices = 2400 cycles = 53% — still fine

### MIDI mapping for an analog synth

```cpp
// config.h for analog synth module
#define MIDI_CC_VCF_CUTOFF     74   // standard: brightness
#define MIDI_CC_VCF_RESONANCE  71   // standard: timbre
#define MIDI_CC_VCF_ENV_AMT    72   // release time (repurposed)
#define MIDI_CC_VCA_ATTACK     73   // attack time
#define MIDI_CC_VCA_DECAY      75   // decay time
#define MIDI_CC_VCA_SUSTAIN    76   // sustain level
#define MIDI_CC_VCA_RELEASE    72   // release time
#define MIDI_CC_LFO_RATE       77   // LFO rate
#define MIDI_CC_LFO_DEPTH      78   // LFO depth
#define MIDI_CC_OSC_DETUNE     94   // oscillator 2 detune
#define MIDI_CC_OSC_WAVE       70   // waveform select
#define MIDI_CC_PORTAMENTO     65   // portamento on/off
#define MIDI_CC_GLIDE_TIME      5   // portamento time
```

### LCD UI for analog synth
The same `lcd.cpp` framebuffer approach works perfectly. Replace the drawbar
bars with:
- Cutoff/resonance display (knob-style arc, or simple bars)
- ADSR envelope shape visualisation (trapezoid graphic)
- LFO waveform icon
- Voice count and patch name in the top bar

The rendering infrastructure (font, `fb_rect`, `fb_hbar`, `fb_str`) is all
reusable — just write new `_draw_top`, `_draw_main`, `_draw_status` functions.