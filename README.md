# Pico Tonewheel Organ — Tone Generator

A Hammond-style tonewheel organ tone generator built on the Raspberry Pi Pico 2 (RP2350), using a Waveshare Pico-Audio Rev 2.1 DAC and Waveshare Pico-LCD-1.14 display. Receives MIDI over USB and/or hardware UART, produces polyphonic tonewheel synthesis with 9 drawbars, Hammond-authentic percussion, key click, and master volume.

This is the **tone generator board** in a two-Pico system. The companion keyboard matrix scanner (a second Pico) sends MIDI to this board over USB or UART. The tone generator can also be driven by any external MIDI source.

---

## Hardware

### Tone Generator Pico

| Module | Part | Notes |
|--------|------|-------|
| MCU | Raspberry Pi Pico 2 (RP2350) | Pico 1 (RP2040) also supported via `pico_tone` env |
| Audio DAC | Waveshare Pico-Audio Rev 2.1 | CS4344, I2S, stacks directly on Pico |
| Display | Waveshare Pico-LCD-1.14 | ST7789V, 240×135, SPI, stacks on Audio via pass-through |

### Stacking Order

```
[Pico-LCD-1.14]   ← top
[Pico-Audio]      ← middle  (pass-through header exposes all Pico GPIO)
[Pico / Pico 2]   ← bottom
```

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

Wire a 6N138 optocoupler between the DIN-5 MIDI connector and GP5. GP4 (UART TX) is unused.

**No GPIO conflicts** between all three stacked boards. All other GPIO remain free for the keyboard matrix scanner or future expansion.

---

## Building

### Requirements

- VSCode + PlatformIO extension
- earlephilhower Arduino-Pico core (installed automatically via `platform-raspberrypi`)

### Environments

```
pico_tone   — Raspberry Pi Pico  (RP2040, 133 MHz, 8 voices)
pico2_tone  — Raspberry Pi Pico 2 (RP2350, 200 MHz, 16 voices)
```

Build and upload:

```bash
pio run -e pico2_tone --target upload
```

### Regenerating audio_pio.pio.h

The PIO I2S program is pre-assembled as `audio_pio.pio.h` and committed. To regenerate if needed:

```bash
pioasm -o c-sdk src/audio_pio.pio src/audio_pio.pio.h
```

Build `pioasm` from the pico-sdk: `cmake` then `make` in `tools/pioasm/`.

---

## Configuration

Everything configurable is in `src/config.h`. No other file needs editing for normal use.

### Feature flags

```cpp
#define ENABLE_LCD      1   // 0 = disable display (useful for audio-only diagnostics)
#define ENABLE_SERIAL   1   // 0 = disable serial debug output
```

Setting both to 0 gives the leanest possible build. The system starts without a USB CDC connection regardless of `ENABLE_SERIAL`.

### Polyphony cap

```cpp
#define MAX_ACTIVE_VOICES  16   // Pico 2 @ 200 MHz: use 16
                                // Pico 2 @ 250 MHz: use 20
                                // Pico 1 @ 133 MHz: use 8
```

When the cap is reached the oldest held note is silenced and the new note takes its slot — graceful degradation rather than engine collapse.

### MIDI channel

```cpp
#define MIDI_CHANNEL  0    // 0 = OMNI (all channels), 1–16 = specific channel
```

### MIDI CC mapping

All CC numbers are defined here and propagated automatically — no other file needs editing.

```cpp
// Drawbars (standard Korg/Roland drawbar controller mapping)
#define MIDI_CC_DRAWBAR_1   12   // 16'
#define MIDI_CC_DRAWBAR_2   13   // 5⅓'
#define MIDI_CC_DRAWBAR_3   14   // 8'   (principal)
#define MIDI_CC_DRAWBAR_4   15   // 4'
#define MIDI_CC_DRAWBAR_5   16   // 2⅔'
#define MIDI_CC_DRAWBAR_6   17   // 2'
#define MIDI_CC_DRAWBAR_7   18   // 1⅗'
#define MIDI_CC_DRAWBAR_8   19   // 1⅓'
#define MIDI_CC_DRAWBAR_9   20   // 1'

// Percussion
#define MIDI_CC_PERC_ONOFF      80   // >= 64 = on
#define MIDI_CC_PERC_HARMONIC   81   // >= 64 = 3rd harmonic, < 64 = 2nd
#define MIDI_CC_PERC_DECAY      82   // >= 64 = fast (~0.3s), < 64 = slow (~1.0s)
#define MIDI_CC_PERC_LEVEL      83   // >= 64 = soft, < 64 = normal

// Click and volume
#define MIDI_CC_CLICK           84   // 0–127 = click level
#define MIDI_CC_VOLUME           7   // standard MIDI volume (CC7)

// Percussion on/off via a note (e.g. footswitch), or -1 to disable
#define MIDI_NOTE_PERC_TOGGLE   -1

// Program Change preset select (0=Full, 1=Jazz, 2=Flute, 3=Off)
#define MIDI_PROGCHANGE_PRESETS  1
```

---

## Serial Commands

Connect at 115200 baud. Commands are newline-terminated. The system starts without waiting for a USB CDC connection.

| Command | Effect |
|---------|--------|
| `help` | List all commands |
| `info` | Print organ state (drawbars, percussion, voices) |
| `pio` | Print audio PIO register state |
| `all` | All notes off |
| `db 888000000` | Set all 9 drawbars (9 digits 0–8) |
| `pre full` | Preset: full organ (888000000) |
| `pre jazz` | Preset: jazz (868800000) |
| `pre flute` | Preset: flute (004020000) |
| `pre off` | All drawbars off |
| `perc on` / `perc off` | Percussion on/off |
| `perc 2` / `perc 3` | Percussion harmonic: 2nd or 3rd |
| `perc fast` / `perc slow` | Percussion decay |
| `perc soft` / `perc norm` | Percussion level |
| `click <0-127>` | Key click level |

---

## Display (Pico-LCD-1.14)

The 240×135 IPS colour LCD shows the organ state in three regions:

```
┌──────────────────────────────────────────┐
│  Preset name    DB3 5/8    8/16v   ●MIDI │  top bar
├──────────────────────────────────────────┤
│  7  7  7  7  7  7  7  7  7               │  level digits
│  ██ ██ ██ ██ ██ ██ ██ ██ ██              │
│  ██ ██ ██ ██ ██ ██ ██ ██ ██              │  ← drawbar bars
│  ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒              │    amber = pulled out (top)
│  ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒              │    dim = pushed in (bottom)
│  16 5.3  8   4  2.7  2  1.6 1.3  1      │  footage labels
├──────────────────────────────────────────┤
│  M● P■ 3 ·  C ●●●●●●···  V ████████▒▒   │  status bar
└──────────────────────────────────────────┘
```

**Top bar:** Preset name — selected drawbar value (e.g. `DB3 5/8`) — voice count as `n/cap` turning red at the polyphony limit — MIDI activity dot (flashes green on any MIDI event).

**Drawbar bars:** The amber region at the **top** of each bar shows how far out the drawbar is pulled, matching the physical orientation of a real Hammond console. Level 8 = fully pulled (all amber). Level 0 = fully pushed in (all dim). The currently selected drawbar is highlighted in white.

**Status bar icons:**

| Icon | Meaning |
|------|---------|
| `M` + dot | MIDI activity (flashes green) |
| `P` + square | Percussion: filled square = on, hollow = off |
| Digit after P | Percussion harmonic: 2 or 3 |
| Small dot (fast/slow) | Percussion decay speed |
| Smaller dot (soft/norm) | Percussion level |
| `C` + 9 dots | Click level (0–9 filled dots = 0–127) |
| `V` + bar | Master volume (CC7, 0–127) |

**Joystick and button controls:**

| Input | Action |
|-------|--------|
| LEFT / RIGHT | Select drawbar (1–9) |
| UP / DOWN | Adjust selected drawbar level (0–8) |
| PRESS | Cycle preset: Full → Jazz → Flute → Full… |
| Button A | Toggle percussion on/off |
| Button B | Toggle percussion harmonic (2nd ↔ 3rd) |

---

## Synthesis Engine

### Tonewheel model

Each voice models one key of a Hammond tonewheel organ. A real Hammond has 91 physical spinning wheels; each key draws from up to 9 simultaneously, one per drawbar. We model this with 9 phase-accumulator oscillators per voice running at the appropriate harmonic multiple of the note frequency.

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

**Wavetable:** 4096-point sine table (12-bit index, ~72 dB SFDR) on Pico 2. 1024-point on Pico 1. Stored in SRAM for access without flash XIP cache pressure.

### Percussion

Authentic Hammond first-note-only percussion:
- Fires only on the **first** note of a new chord — resets when all keys are released
- When percussion is enabled, the 8' drawbar is reduced by 25% (authentic: the original circuit shared this bus)
- Switchable between 2nd harmonic (one octave up) and 3rd harmonic (quint above octave)
- Fast decay (~0.3 s) or slow decay (~1.0 s)
- Normal or soft level
- Volume tracks the overall drawbar sum so it stays proportional at any registration

### Key click

A short filtered noise burst on every note-on and note-off, modelling the mechanical relay contact of the original organ. Level adjustable 0–127 via CC84. Tracks drawbar sum for proportional volume.

### Master volume

CC7 (standard MIDI volume) scales the final mix 0–127. Applied after all voice mixing, percussion and click, before the soft clip. The `V` bar on the display reflects the current value.

### Dual-core architecture

```
Core 0 (loop):   MIDI polling → noteOn/noteOff → LCD update → serial
Core 1 (loop1):  Audio hot path — tick() → PIO FIFO  [runs from SRAM]
```

`loop1()` is marked `__not_in_flash_func` so it runs from SRAM rather than flash, avoiding XIP cache misses in the 144-oscillator inner loop.

**Spinlock synchronisation** between cores uses `spin_lock_unsafe_blocking()` (not `spin_lock_blocking()`). The distinction is critical: the standard `_blocking` variant disables interrupts while spinning, which would cause TinyUSB to miss USB microframe interrupts during lock contention on a chord burst, stalling the MIDI input pipe. The `_unsafe` variant leaves interrupts enabled — safe here because no interrupt handler on core 0 touches voice state.

Core 1 `tick()` uses `spin_try_lock_unsafe()` — non-blocking. If core 0 holds the lock, core 1 uses current voice state for one buffer (256 samples = 5.8 ms) and never stalls. This prevents the CS4344 hardware automute that would otherwise trigger if LRCK stopped toggling due to a stalled PIO FIFO.

### Pico 2 advantages over Pico 1

| Feature | Pico 1 (RP2040) | Pico 2 (RP2350) |
|---------|-----------------|-----------------|
| Core | Cortex-M0+ | Cortex-M33 |
| Clock | 133 MHz | 150 MHz stock, 200 MHz used here |
| Hardware FPU | No (float ≈ 100 cycles) | Yes (float = 1 cycle) |
| Hardware divide | No (/ ≈ 20–35 cycles) | Yes (/ = 1 cycle) |
| SRAM | 264 KB | 520 KB |
| Safe voices (full drawbars) | ~8 | 16 at 200 MHz, 20 at 250 MHz |

At 200 MHz with hardware divide and FPU, the Pico 2 handles 16 full-drawbar voices with significant headroom. The FPU makes the float percussion and click envelope arithmetic essentially free, allowing cleaner code without integer workaround approximations.

---

## File Structure

```
platformio.ini          Build config — pico_tone (RP2040) and pico2_tone (RP2350)

src/
  config.h              ALL configuration: feature flags, MIDI CCs, polyphony, audio
  main.cpp              Core 0 setup/loop, Core 1 audio loop, MIDI callbacks, serial

  audio_driver.h        PIO I2S driver (init, blocking put, debug)
  audio_pio.pio         Waveshare PIO I2S source (original Waveshare program)
  audio_pio.pio.h       Pre-assembled PIO program — committed, no build-time pioasm

  wavetable.h           Sine LUT generation (runtime init, stored in SRAM)
  oscillator.h          Phase-accumulator oscillator with integer amplitude (0–256)
  tonewheel_voice.h     9-oscillator voice (one per drawbar partial)
  tonewheel_manager.h   Polyphonic manager: voice pool, spinlock, drawbar cache,
                        master volume, polyphony cap with oldest-voice stealing
  drawbars.h            9-drawbar state, footage ratios, CC handling (reads config.h)
  percussion.h          Hammond percussion: first-note trigger, float envelope (FPU)
  click.h               Key click noise burst with decaying envelope
  midi_handler.h        USB-MIDI + UART MIDI parser, channel filter, program change

  lcd.h                 LCD public API — declarations only, no implementation
  lcd.cpp               ST7789V driver, full 64 KB framebuffer (Pico 2),
                        region renderers (top/bars/status), button handler

  voice.h               Simple monophonic voice (reference, unused in main build)
  voice_manager.h       Simple polyphonic manager (reference, unused in main build)
```

---

## Technical Notes

### Why the Waveshare PIO driver instead of the earlephilhower I2S library

The Pico-Audio Rev 2.1 has BCK on GP26 and LRCK on GP27. The earlephilhower I2S library puts LRCK on `BCLK_pin + 1`, which physically matches (GP26+1 = GP27). However the library's PIO program assigns BCK to side-set bit 1 and LRCK to bit 0 — the opposite of the Waveshare board's wiring. The result is inverted left/right channels and no audio output. The Waveshare-supplied PIO program uses `.side_set 2` with BCK as bit 0 and LRCK as bit 1, matching the hardware exactly, and is used verbatim.

### USB-MIDI dropout under chord bursts

The original implementation used `spin_lock_blocking()` for voice state synchronisation. This function saves and disables interrupts while spinning. TinyUSB's USB stack processes USB microframes via interrupt (every 125 µs for USB full-speed). When playing a chord burst of 10+ notes rapidly, each `noteOn()` called `spin_lock_blocking()`, cumulatively blocking interrupts long enough for TinyUSB to miss microframe deadlines. The USB host would then declare the device unresponsive and stop sending MIDI packets — appearing as engine collapse. The fix is `spin_lock_unsafe_blocking()`, which never disables interrupts.

### CS4344 hardware automute

The CS4344 DAC has a built-in automute that engages if it detects LRCK irregularity for approximately 200 ms. If the audio core stalled (e.g. due to blocked `spin_lock_blocking()` calls causing FIFO underrun), LRCK would stop toggling, the CS4344 would automute, and sound would stop and not recover until clocks were stable again. The non-blocking `spin_try_lock_unsafe()` in `tick()` prevents this: if the lock is held, core 1 uses slightly stale voice state for one buffer (5.8 ms, inaudible) rather than stalling.

---

## Known Limitations and Next Steps

- **Keyboard matrix scanner** — the second Pico that scans the physical keyboard and sends MIDI to this board. Next development phase.
- **Leslie / rotary speaker** effect — not yet implemented. Could be done in software as a periodic pitch/amplitude modulation applied to the final mix, or routed to an external hardware unit.
- **Vibrato / chorus** — Hammond V/C tabs. Vibrato is a periodic pitch deviation; chorus adds a slightly detuned copy. Both are achievable in the synthesis engine.
- **Velocity sensitivity** — the original Hammond ignores velocity (all-or-nothing key contacts). However a soft velocity-to-volume mapping could be added as a non-authentic option.
- **UART MIDI from scanner** — the keyboard scanner Pico will connect to GP5 (UART RX) at 31250 baud. The UART MIDI parser in `midi_handler.h` is already implemented and waiting.