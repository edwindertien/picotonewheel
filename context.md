# Project Context — Pico Tonewheel Organ (USB Host + Effects build)

This is the handoff document for the `tonewheel_usb` build. Read alongside
`README.md` before making any changes. Contains architecture decisions,
hard-won lessons, and the current state of every component.

This build extends `tonewheel_p2` with:
- USB MIDI host port (GP6/GP7)
- Post-processing effects chain (overdrive, vibrato, chorus)
- Extended joystick menu (drawbars + effect params)
- CPU load indicator (DWT cycle counter)

All rules from `tonewheel_p2/CONTEXT.md` still apply. This document covers
what changed and what was learned.

---

## What changed from tonewheel_p2

### Audio on PIO2

`audio_driver.h` uses `_pio = pio2`. `audio_driver_init()` must be called
before `usb_host_init()` so PIO2 is claimed first.

PIO layout:
```
PIO0: USB TX  (22 instructions)
PIO1: USB RX  (32 instructions — exactly full)
PIO2: Audio   (13 instructions)
```

### Clock: 200 → 240 MHz

USB host requires clean PIO divisors. 240 MHz gives USB TX ÷5, RX ÷2.5
exactly. Audio clkdiv recalculates at runtime via `clock_get_hz()`.

### New file: usb_host.h

USB MIDI host. `usb_host_init()` / `usb_host_poll()` + all four TinyUSB
MIDI host callbacks with correct signatures for latest TinyUSB (3.7.x):
```cpp
void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *cb_data);
void tuh_midi_umount_cb(uint8_t idx);
void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes);
void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes);
```
Parameter is `idx` (interface index), not `dev_addr`. Wrong signatures give
a clear compile error showing the expected signature.

### New file: effects.h

Three post-processing effects, all in one file. Applied in `tick()` after
voice mixing, before master volume and final clip.

### lib/pio_usb/ compatibility stub

`pio_usb_host.c` has a stub `pio_usb_host_endpoint_close()` appended.
`pio_usb.h` has its declaration. Required for Adafruit TinyUSB ≥3.5.

---

## Effects architecture

All effects run on core 1 inside `tick()`. Parameters are `uint8_t` — ARM
single-byte writes are atomic, no spinlock needed for core 0 updates.

### Chain order in tick()

```
voices × 16 → mix/3 → percussion → click
    → overdrive → chorus → masterVolume → clip → int16
```

Vibrato is applied per-voice inside the voice loop (before mixing).

### 1. Overdrive (`effects.h` — `Overdrive` struct)

**Asymmetric rational waveshaper** — different curves for positive/negative:
```cpp
if (x >= 0): y = x / (1 + x)          // smooth rolloff
if (x <  0): y = x / (1 - 0.5x)       // slightly harder
```
Asymmetry generates even harmonics (2nd, 4th) = tube/valve character.
The old cubic shaper generated only odd harmonics (3rd, 5th) = transistor-harsh.
Input gain 1.0..5.0 (drive 1..127), output compensated by 1/g.
CC 85, serial `drive <0-127>`.

### 2. Vibrato (`effects.h` — `Vibrato` struct)

Sine LFO, `pitchMult` output read by `TonewheelManager::tick()` and passed
to `TonewheelVoice::tick(pitchMult)`, which calls `Oscillator::applyPitchMult()`
directly on `phaseInc`. This avoids `powf()` in the hot path.

`applyPitchMult()` multiplies from `_basePhaseInc` (stored at `setFrequency()`
time) not from the current `phaseInc` — so modulation never accumulates/drifts.

Max depth controlled by `VIBRATO_MAX_SEMITONES` in `config.h` (default 0.25).
0.75 was the original value — far too heavy. 0.25 = authentic Hammond scanner.
CC 86 (depth) + 87 (rate), serial `vib d/r <0-127>`.

### 3. Chorus (`effects.h` — `Chorus` struct)

**Dual quadrature BBD chorus** (Boss CE-2 architecture). Two delay lines,
LFOs 90° apart:
```
lfo0 = sin(phase)         delay line A
lfo1 = sin(phase + 0.25)  delay line B (cos)
wet  = (wetA + wetB) / 2
```
When one LFO is at turnaround (fastest pointer movement, most artifact-prone),
the other is at its flattest — artifacts cancel. This eliminates the periodic
"loopround click" that a single delay line produces.

**Modulation range**: ±2ms max (depth=127 → ±1ms swing). The original 5ms
range gave ±44 cents pitch deviation at mid-depth — far too extreme, sounded
like a pitch loop. 2ms gives ±~20 cents max = subtle and musical.

**Wet mix**: 0..50% controlled by separate `mix` parameter (CC 90, default 64).
Depth and mix are independent — depth controls pitch wobble character,
mix controls how present the effect is.

Two delay buffers: 2 × 2048 × 2 bytes = 8 KB SRAM.
CC 88 (depth) + 89 (rate) + 90 (mix), serial `cho d/r/m <0-127>`.

---

## CPU load measurement

`loop1()` measures tick() compute time using the DWT cycle counter.
Standard CMSIS `CoreDebug`/`DWT` structs are not exposed in arduino-pico —
use raw register pointers instead:

```cpp
#define _DCB_DEMCR  (*((volatile uint32_t*)0xE000EDFC))  // bit 24 = TRCENA
#define _DWT_CYCCNT (*((volatile uint32_t*)0xE0001004))  // cycle counter
#define _DWT_CTRL   (*((volatile uint32_t*)0xE0001000))  // bit 0 = CYCCNTENA
```

These are architectural constants on all Cortex-M33 cores.
Accumulated per-sample cycle counts / budget cycles = load%.

**Critical**: do NOT use double-pass (compute all → push all). The PIO FIFO
holds only 8 samples (181µs). During a 3ms compute-only pass the FIFO drains,
LRCK stops, and the CS4344 hardware automute triggers — exactly the same
failure mode as the wrong spinlock. Always interleave compute and put.

IIR filter: fast attack (α≈0.94), fast decay (α≈0.19) so indicator responds
quickly in both directions. Displayed as 7×7 square in top bar.

---

## Extended joystick menu

`_sel` ranges 0..16:
- 0..8 = drawbars 1–9 (UP/DOWN adjusts level 0–8, step 1)
- 9 = click (step 8, range 0–127)
- 10 = master volume (step 16, range 0–255)
- 11 = overdrive drive (step 8)
- 12 = vibrato depth (step 8)
- 13 = vibrato rate (step 8)
- 14 = chorus depth (step 8)
- 15 = chorus rate (step 8)
- 16 = chorus mix (step 8)

Top bar shows `DB3  5/8` for drawbars, `Vib Dpt  42` for effect params.
A 2px cyan bar appears above the relevant status section when _sel > 8.
`ui_mark_dirty_bottom()` must be called on LEFT/RIGHT navigation so the
cursor bar redraws (and disappears when returning to drawbar mode).

---

## Critical rules inherited from tonewheel_p2

**Spinlock**: use `spin_lock_unsafe_blocking()` on core 0, `spin_try_lock_unsafe()`
on core 1. Never `spin_lock_blocking()` — disables IRQs, breaks USB-MIDI.

**CS4344 automute**: if LRCK stops for ~200ms the DAC automutes silently.
Core 1 must never stall. The non-blocking try-lock and interleaved compute+put
both serve this requirement.

**Callbacks**: `tuh_mount_cb` and `tuh_midi_mount_cb` must not call any `_sync`
TinyUSB functions — doing so blocks `USBHost.task()` and can prevent hub-connected
devices from enumerating.

---

## Keyboard scanner — next phase

Scanner Pico sends UART MIDI at 31250 baud to GP5. `midi_handler.h` already
listens on `Serial2` (UART1). Wire GP0 (scanner TX) → GP5 (tone gen RX) + GND.
No optocoupler needed between two Picos on the same ground.

---

## File checklist

```
platformio.ini
README.md / CONTEXT.md

src/
  config.h              — ALL config including effects CC map and scaling
  main.cpp              — Core 0/1, MIDI callbacks, serial, CPU load
  audio_driver.h        — PIO2 I2S
  audio_pio.pio/.pio.h
  usb_host.h            — USB MIDI host, TinyUSB callbacks
  midi_handler.h        — USB device + UART MIDI
  effects.h             — Overdrive, Vibrato, Chorus
  wavetable.h
  oscillator.h          — has applyPitchMult() + _basePhaseInc for vibrato
  tonewheel_voice.h     — tick(pitchMult) passes vibrato to oscillators
  tonewheel_manager.h   — effect objects, handleFxCC(), effects in tick()
  drawbars.h
  percussion.h
  click.h
  lcd.h / lcd.cpp       — extended selector, CPU square, cursor bar

lib/
  pio_usb/
    pio_usb.h           — has endpoint_close declaration
    pio_usb_host.c      — has endpoint_close stub at end
    (+ other files)
```

---

## Planned next modules (separate projects, shared platform)

- **Analog synth** — VCO/VCF/ADSR, Moog ladder filter. Design sketch in
  `tonewheel_p2/CONTEXT.md`. Start from `tonewheel_usb` as platform base.
- **FM synth** — operator-based, algorithm display on LCD
- **E-piano** — wavetable/sample playback
- **TR-808 clone** — step sequencer, different main loop architecture

All share: `audio_driver.h`, `midi_handler.h`, `usb_host.h`, `lcd.h/cpp`
(drawing primitives), `oscillator.h`, `wavetable.h`. Only voice engine,
voice manager, and LCD UI differ per module.