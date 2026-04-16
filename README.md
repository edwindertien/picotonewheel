# Pico Tonewheel Organ — Tone Generator

A Hammond-style tonewheel organ tone generator built on the Raspberry Pi Pico / Pico 2, using a [Waveshare Pico-Audio Rev 2.1 DAC](https://www.waveshare.com/wiki/Pico-Audio) and Waveshare Pico-LCD-1.14 display. Receives MIDI over USB and/or hardware UART, produces polyphonic tonewheel synthesis with 9 drawbars, Hammond-authentic percussion, and key click.

This is the **tone generator board** in a two-Pico system. The companion keyboard matrix scanner (Pico 1) sends MIDI to this board. The tone generator can also be driven by any external MIDI source.

---

## Hardware

### Tone Generator Pico

| Module | Part | Notes |
|--------|------|-------|
| MCU | Raspberry Pi Pico 2 (RP2350) | Pico 1 (RP2040) also supported |
| Audio DAC | Waveshare Pico-Audio Rev 2.1 | CS4344, I2S, stacks on Pico |
| Display | Waveshare Pico-LCD-1.14 | ST7789V, 240×135, SPI, stacks on Audio via pass-through |

### Stacking Order

```
[Pico-LCD-1.14]   ← top
[Pico-Audio]      ← middle (pass-through pins expose all Pico GPIO)
[Pico / Pico 2]   ← bottom
```

The Pico-Audio pass-through header exposes all Pico GPIO so the LCD can be stacked on top without pin conflicts.

### Pin Assignments

**Pico-Audio Rev 2.1 (I2S, hard-wired on PCB):**

| Signal | GPIO |
|--------|------|
| I2S DIN (audio data) | GP22 |
| I2S BCK (bit clock) | GP26 |
| I2S LRCK (word select) | GP27 |

**Pico-LCD-1.14 (SPI1, hard-wired on PCB):**

| Signal | GPIO |
|--------|------|
| SCK | GP10 |
| MOSI | GP11 |
| CS | GP9 |
| DC | GP8 |
| RST | GP12 |
| Backlight | GP13 |

**Pico-LCD-1.14 Joystick & Buttons:**

| Input | GPIO |
|-------|------|
| Joystick UP | GP2 |
| Joystick DOWN | GP18 |
| Joystick LEFT | GP16 |
| Joystick RIGHT | GP20 |
| Joystick PRESS | GP3 |
| Button A | GP15 |
| Button B | GP17 |

**MIDI UART (hardware MIDI-in via optocoupler):**

| Signal | GPIO |
|--------|------|
| UART RX | GP5 |

Connect a 6N138 optocoupler between the DIN-5 MIDI connector and GP5. GP4 (TX) is unused.

---

## Building

### Requirements

- VSCode + PlatformIO extension
- earlephilhower Arduino-Pico core (installed via `platform-raspberrypi`)

### platformio.ini environments

```
pico_tone   — Raspberry Pi Pico  (RP2040, 133 MHz)
pico2_tone  — Raspberry Pi Pico 2 (RP2350, 200 MHz overclocked)
```

Build and upload:

```bash
pio run -e pico2_tone --target upload
```

### Generating audio_pio.pio.h

The PIO I2S program is pre-assembled as `audio_pio.pio.h` and committed to the repository. If you ever need to regenerate it:

```bash
pioasm -o c-sdk src/audio_pio.pio src/audio_pio.pio.h
```

`pioasm` can be built from the pico-sdk: `cmake` then `make` in `tools/pioasm/`.

---

## Configuration

Everything is in `src/config.h`. You should not need to edit any other file for basic customisation.

### Feature flags

```cpp
#define ENABLE_LCD      1   // 0 = disable display entirely
#define ENABLE_SERIAL   1   // 0 = disable serial debug output
```

Setting `ENABLE_LCD 0` and `ENABLE_SERIAL 0` gives the leanest possible audio-only build for diagnostics.

### Polyphony

```cpp
#define MAX_ACTIVE_VOICES  16   // Pico 2 @ 200 MHz
                                // Use 8 for Pico 1 @ 133 MHz
                                // Use 20 for Pico 2 @ 250 MHz
```

When the cap is reached, the oldest held note is silenced and the new note takes its slot — graceful degradation rather than engine collapse.

### MIDI channel

```cpp
#define MIDI_CHANNEL  0    // 0 = OMNI (respond to all channels)
                           // 1-16 = specific channel
```

### MIDI CC mapping

```cpp
// Drawbars — 9 CCs for the 9 footage positions
#define MIDI_CC_DRAWBAR_1   12   // 16'
#define MIDI_CC_DRAWBAR_2   13   // 5⅓'
#define MIDI_CC_DRAWBAR_3   14   // 8'    (principal)
#define MIDI_CC_DRAWBAR_4   15   // 4'
#define MIDI_CC_DRAWBAR_5   16   // 2⅔'
#define MIDI_CC_DRAWBAR_6   17   // 2'
#define MIDI_CC_DRAWBAR_7   18   // 1⅗'
#define MIDI_CC_DRAWBAR_8   19   // 1⅓'
#define MIDI_CC_DRAWBAR_9   20   // 1'

// Percussion
#define MIDI_CC_PERC_ONOFF      80   // value >= 64 = on
#define MIDI_CC_PERC_HARMONIC   81   // value >= 64 = 3rd harmonic
#define MIDI_CC_PERC_DECAY      82   // value >= 64 = fast
#define MIDI_CC_PERC_LEVEL      83   // value >= 64 = soft

// Click and volume
#define MIDI_CC_CLICK           84   // 0-127 = click level
#define MIDI_CC_VOLUME           7   // standard MIDI volume

// Percussion toggle via note-on (e.g. footswitch), -1 to disable
#define MIDI_NOTE_PERC_TOGGLE   -1

// Program Change selects preset: 0=Full, 1=Jazz, 2=Flute, 3=Off
#define MIDI_PROGCHANGE_PRESETS  1
```

The default drawbar CC mapping (CC 12–20) matches the standard Korg/Roland drawbar controller convention used by most MIDI drawbar modules and DAWs.

---

## Serial Commands

Connect at 115200 baud. Commands are newline-terminated.

| Command | Effect |
|---------|--------|
| `help` | List all commands |
| `info` | Print organ state (drawbars, percussion, active voices) |
| `pio` | Print audio PIO register state |
| `all` | All notes off |
| `db 888000000` | Set all 9 drawbars (9 digits 0–8, e.g. `db 868000000`) |
| `pre full` | Preset: full organ (888000000) |
| `pre jazz` | Preset: jazz (868800000) |
| `pre flute` | Preset: flute (004020000) |
| `pre off` | Preset: all drawbars off |
| `perc on` / `perc off` | Percussion on/off |
| `perc 2` / `perc 3` | Percussion harmonic: 2nd or 3rd |
| `perc fast` / `perc slow` | Percussion decay |
| `perc soft` / `perc norm` | Percussion level |
| `click <0-127>` | Key click level |

---

## Display (Pico-LCD-1.14)

The 240×135 colour LCD shows the organ state in three regions:

```
┌──────────────────────────────────────────┐
│  Preset name   DB3 5/8   12/16v   ●MIDI  │  ← top bar
├──────────────────────────────────────────┤
│  7 7 7 7 7 7 7 7 7   ← level digits      │
│  ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐  │
│  │█│ │█│ │█│ │ │ │ │ │ │ │ │ │ │ │ │   │  ← drawbar bars
│  │█│ │█│ │█│ │ │ │ │ │ │ │ │ │ │ │ │   │    pulled out = amber at top
│  └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘  │
│  16  5.3  8   4  2.7  2  1.6 1.3  1     │  ← footage labels
├──────────────────────────────────────────┤
│  M● P■ 3 ● ·  C ●●●●●●···  V ████████  12/16 │  ← status bar
└──────────────────────────────────────────┘
```

**Drawbars:** The amber region at the top of each bar represents how far out the drawbar is pulled. Level 8 = fully pulled (all amber). Level 0 = fully pushed in (all dim). The selected drawbar is shown in white.

**Status bar icons:**
- `M` + dot — MIDI activity flash
- `P` + square — Percussion (filled = on, hollow = off), harmonic digit, fast/slow dot, soft/norm dot  
- `C` + 9 dots — Click level
- `V` + bar — Volume (placeholder)
- Large number — Voice count / cap, turns red when at polyphony limit

**Joystick controls:**

| Input | Action |
|-------|--------|
| LEFT / RIGHT | Select drawbar (1–9) |
| UP / DOWN | Adjust selected drawbar level (0–8) |
| PRESS | Cycle preset (Full → Jazz → Flute → repeat) |
| Button A | Toggle percussion on/off |
| Button B | Toggle percussion harmonic (2nd ↔ 3rd) |

---

## Synthesis Engine

### Tonewheel model

Each voice models one key of a Hammond tonewheel organ. A real Hammond has 91 physical spinning metal wheels; each key draws from up to 9 of them simultaneously, one per drawbar. We model this with 9 phase-accumulator oscillators per voice, each running at the appropriate harmonic multiple of the note frequency.

**Footage and harmonic multipliers:**

| Drawbar | Footage | Multiplier | Character |
|---------|---------|------------|-----------|
| 1 | 16' | × 0.5 | Sub-octave |
| 2 | 5⅓' | × 1.5 | Sub-fifth (nazard) |
| 3 | 8' | × 1.0 | Unison (principal) |
| 4 | 4' | × 2.0 | Octave |
| 5 | 2⅔' | × 3.0 | Quint |
| 6 | 2' | × 4.0 | Super-octave |
| 7 | 1⅗' | × 5.0 | Tierce |
| 8 | 1⅓' | × 6.0 | Larigot |
| 9 | 1' | × 8.0 | Sifflöte |

**Wavetable:** 4096-point sine table (12-bit index) on Pico 2, 1024-point on Pico 1. Stored in SRAM for fast access without flash XIP cache pressure.

### Percussion

Authentic Hammond first-note-only percussion:
- Fires only on the **first** note of a new chord
- Resets when all keys are released
- Steals a small amount of the 8' drawbar signal (authentic hardware behaviour — the original circuit shared the bus)
- Switchable between 2nd harmonic (octave above unison) and 3rd harmonic (quint above octave)
- Fast decay (~0.3 s) or slow decay (~1.0 s)
- Normal or soft level
- Volume tracks the overall drawbar sum so it stays in proportion at any registration

### Key click

A short filtered noise burst on every note-on and note-off, modelling the mechanical relay contact of the original organ. Level adjustable 0–127 via CC84. Also tracks drawbar sum for proportional volume.

### Dual-core architecture

```
Core 0 (loop):   MIDI polling → noteOn/noteOff → LCD update → serial commands
Core 1 (loop1):  Audio hot path — tick() → PIO FIFO  [runs from SRAM]
```

Core 1 (`loop1`) is marked `__not_in_flash_func` so it executes from SRAM rather than flash. This avoids XIP cache misses in the 144-oscillator inner loop, which would otherwise multiply effective cycle cost by 10–30×.

A hardware RP2040/RP2350 spinlock synchronises voice state between cores:
- `noteOn()` / `noteOff()` on core 0: **blocking** lock (MIDI rate, can wait a few µs)
- `tick()` on core 1: **non-blocking** try-lock — if core 0 holds the lock, core 1 uses current voice state for one buffer (256 samples = 5.8 ms, inaudible) and never stalls

### Pico 2 optimisations

The RP2350 Cortex-M33 core adds:

| Feature | Benefit |
|---------|---------|
| Hardware FPU | `powf()`, float envelope multiply = 1 cycle (was ~100) |
| Hardware divide | `mix /= 3`, `mix /= 10` = 1 cycle (was ~20–35 software) |
| 520 KB SRAM | Full 64 KB LCD framebuffer; 4096-point wavetable |
| 200 MHz overclock | 1.5× headroom vs Pico 1 at 133 MHz |

At 200 MHz with hardware divide, 16-voice polyphony is achievable with all drawbars full. At 250 MHz, 20 voices.

---

## File Structure

```
platformio.ini          Build configuration — two environments (pico_tone, pico2_tone)

src/
  config.h              ALL configuration: feature flags, MIDI CCs, audio params
  main.cpp              Core 0 setup/loop, Core 1 audio loop, MIDI callbacks
  
  audio_driver.h        PIO I2S driver init and put functions
  audio_pio.pio         Waveshare PIO I2S program source (Waveshare original)
  audio_pio.pio.h       Pre-assembled PIO program (committed — no build-time pioasm)
  
  wavetable.h           Sine LUT generation (runtime, stored in SRAM)
  oscillator.h          Phase-accumulator oscillator, integer amplitude path
  tonewheel_voice.h     9-oscillator voice (one per drawbar partial)
  tonewheel_manager.h   Polyphonic manager: voice pool, spinlock, drawbar sum cache
  drawbars.h            9-drawbar state, footage ratios, CC handling
  percussion.h          Hammond percussion: first-note trigger, float envelope
  click.h               Key click noise burst
  midi_handler.h        USB-MIDI + UART MIDI parser, channel filter, program change
  
  lcd.h                 LCD public API (declarations only — no implementation)
  lcd.cpp               ST7789V driver, full framebuffer, UI renderer, button handler
  
  voice.h               Simple monophonic voice (reference, unused in main build)
  voice_manager.h       Simple polyphonic manager (reference, unused in main build)
```

---

## Known Limitations & Next Steps

- **Master volume** (CC7) is received but not yet implemented — the `V` bar on the display is a placeholder
- **Leslie / rotary speaker** effect not yet implemented
- **Keyboard matrix scanner** (the second Pico) is the next development phase — it will scan a physical keyboard matrix and transmit MIDI to this board over USB or UART
- **Vibrato / chorus** (Hammond V/C tabs) not yet implemented

---

## Hardware Notes

### Why the Waveshare PIO driver instead of the earlephilhower I2S library?

The Pico-Audio Rev 2.1 has BCK on GP26 and LRCK on GP27. The earlephilhower I2S library always puts LRCK on `BCLK_pin + 1`, so for LRCK=GP27 it requires BCK=GP26 — which happens to match. However, the pin **order** within the PIO side-set matters: the Waveshare board expects BCK as side-set bit 0 and LRCK as side-set bit 1. The earlephilhower library uses the opposite convention, resulting in inverted left/right channels and no audio. The Waveshare-supplied PIO program (`audio_pio.pio`) uses `.side_set 2` with the correct bit assignments and is used verbatim.

### Stacking conflict check

No GPIO conflicts between the three stacked boards:

| Board | Pins used |
|-------|-----------|
| Pico-Audio | GP22, GP26, GP27 |
| Pico-LCD-1.14 (SPI) | GP8, GP9, GP10, GP11, GP12, GP13 |
| Pico-LCD-1.14 (buttons) | GP2, GP3, GP15, GP16, GP17, GP18, GP20 |
| MIDI UART | GP5 |

All other GPIO remain free for the keyboard matrix scanner or future expansion.