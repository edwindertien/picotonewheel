# Pico Tonewheel Organ — Tone Generator (USB Host + Effects build)

A Hammond-style tonewheel organ tone generator built on the Raspberry Pi Pico 2 (RP2350), using a Waveshare Pico-Audio Rev 2.1 DAC and Waveshare Pico-LCD-1.14 display. Receives MIDI from three simultaneous sources merged into the synthesis engine:

- **USB device** (Pico's native USB port) — PC, DAW, or USB MIDI controller
- **USB host** (GP6/GP7 socket) — one USB MIDI keyboard or controller
- **UART** (GP5) — keyboard matrix scanner Pico (next build phase)

Post-processing effects chain: overdrive → vibrato → chorus.

---

## Hardware

### Stacking Order

```
[Pico-LCD-1.14]   ← top
[Pico-Audio]      ← middle
[Pico 2]          ← bottom
```

### Pin Assignments

**Pico-Audio Rev 2.1 (I2S — hard-wired on PCB):**

| Signal | GPIO | Notes |
|--------|------|-------|
| I2S DIN | GP22 | Audio data |
| I2S BCK | GP26 | Bit clock |
| I2S LRCK | GP27 | Word select |

Audio uses **PIO2** (not PIO0) to leave PIO0/PIO1 free for USB host.

**Pico-LCD-1.14 (SPI1 — hard-wired on PCB):**

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

**USB MIDI host (USB-A socket — wire yourself):**

| Signal | GPIO | Notes |
|--------|------|-------|
| D+ | GP6 | 15kΩ pull-down to GND (host mode) |
| D- | GP7 | Direct |
| VBUS | 5V / VSYS | Powers connected device |
| GND | GND | |

USB-A pinout (looking into socket, left to right): VBUS(red) · D−(white) · D+(green) · GND(black).

**MIDI UART (hardware MIDI-in or scanner Pico):**

| Signal | GPIO |
|--------|------|
| UART RX | GP5 |

---

## Building

### Environments

```
pico2_tone    — Pico 2 (RP2350, 240 MHz) — full build with USB host + effects
pico2_nohost  — Pico 2 (RP2350, 200 MHz) — no USB host (audio/MIDI test)
pico_tone     — Pico 1 (RP2040, 133 MHz) — no USB host, no DWT cycle counter
```

```bash
pio run -e pico2_tone --target upload
```

---

## Configuration (config.h)

All user-facing values are in `src/config.h`.

### Feature flags

```cpp
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#define ENABLE_USB_HOST   1   // 0 = disable USB host socket
```

### Polyphony and audio

```cpp
#define MAX_ACTIVE_VOICES  16   // reduce if CPU load indicator goes red
#define SAMPLE_RATE        44100
#define BUFFER_FRAMES      256
```

### Effects scaling

```cpp
#define VIBRATO_MAX_SEMITONES  0.25f  // max pitch deviation at depth=127
                                       // 0.25 = authentic Hammond
                                       // 0.5  = noticeable, 1.0 = strong
#define CHORUS_MIX_DEFAULT     64     // default wet mix (0..127 → 0..50%)
```

### MIDI CC map

| CC | Parameter | Range |
|----|-----------|-------|
| 7 | Master volume | 0–127 |
| 12–20 | Drawbars 1–9 | 0–127 |
| 80 | Percussion on/off | ≥64 = on |
| 81 | Percussion harmonic | ≥64 = 3rd |
| 82 | Percussion decay | ≥64 = fast |
| 83 | Percussion level | ≥64 = soft |
| 84 | Key click level | 0–127 |
| 85 | Overdrive | 0=off, 1–127 |
| 86 | Vibrato depth | 0=off, 1–127 |
| 87 | Vibrato rate | 0–127 (0.5–9 Hz) |
| 88 | Chorus depth | 0=off, 1–127 |
| 89 | Chorus rate | 0–127 (0.2–3 Hz) |
| 90 | Chorus mix | 0–127 (0–50% wet) |

---

## Serial Commands

Connect at 115200 baud.

| Command | Effect |
|---------|--------|
| `help` | List all commands |
| `info` | Organ state |
| `all` | All notes off |
| `db 888000000` | Set drawbars (9 digits 0–8) |
| `pre full\|jazz\|flute\|off` | Load preset |
| `perc on\|off\|2\|3\|fast\|slow\|soft\|norm` | Percussion |
| `click <0-127>` | Key click level |
| `vol <0-127>` | Master volume |
| `drive <0-127>` | Overdrive (0=off) |
| `vib d <0-127>` | Vibrato depth |
| `vib r <0-127>` | Vibrato rate |
| `cho d <0-127>` | Chorus depth |
| `cho r <0-127>` | Chorus rate |
| `cho m <0-127>` | Chorus wet mix |
| `pio` | Audio PIO debug |

---

## Display

### Top bar (y=0..17)

```
[Preset name]  [Param name + value]  [voices]  [CPU]  [MIDI]
```

- **Preset name** — Custom / Full organ / Jazz / Flute
- **Centre** — selected drawbar `DB3  5/8` or effect param `Vib Dpt  42`
- **Voice count** — `n/16`, turns red at polyphony cap
- **CPU square** — green <50%, orange 50–79%, red ≥80%
- **MIDI dot** — flashes green on any MIDI event

### Drawbar area (y=19..108)

9 amber bars, amber at top = pulled out. The selected bar/parameter is highlighted.

### Status bar (y=110..134)

```
M●  P■ 3 ·  C ■■□□  V ■■■□  D ■□□□  ~ ::::  W ::::
                                             ^ depth  ^ depth
                                               rate     mix
```

| Symbol | Meaning |
|--------|---------|
| `M` + dot | MIDI activity |
| `P` + square | Percussion on/off, harmonic, fast/slow, soft/norm |
| `C` + 4 squares | Click level (0–4 filled) |
| `V` + 4 squares | Master volume (0–4 filled) |
| `D` + 4 squares | Overdrive (0–4 filled) |
| `~` + 2×4 squares | Vibrato: top row = depth, bottom row = rate |
| `W` + 2×4 squares | Chorus: top row = depth, bottom row = mix |

A **cyan underline** appears above the status section currently selected by the joystick.

### Joystick navigation

| Direction | Action |
|-----------|--------|
| LEFT / RIGHT | Navigate: drawbars 1–9, then click, volume, drive, vib-depth, vib-rate, cho-depth, cho-rate, cho-mix |
| UP / DOWN | Adjust selected parameter (drawbars: ±1 level; effects: ±8 steps) |
| PRESS | Cycle preset: Full → Jazz → Flute → Full… |
| Button A | Cycle percussion presets |
| Button B | Toggle click on/off |

---

## Effects

### Overdrive

Asymmetric tube-style waveshaper. Positive and negative halves clip at different rates, generating even harmonics (2nd, 4th) characteristic of valve amplifiers. Sounds warm rather than harsh. Unity-gain compensated — output level stays close to bypass regardless of drive amount. CC 85, serial `drive <0-127>`.

### Vibrato

Sine LFO modulates oscillator frequency via direct `phaseInc` manipulation — no `powf()` in the audio path. Max depth set by `VIBRATO_MAX_SEMITONES` in `config.h` (default 0.25 semitones = authentic Hammond scanner vibrato). CC 86 (depth) + 87 (rate), serial `vib d/r <0-127>`.

### Chorus

Dual quadrature BBD-style chorus (Boss CE-2 architecture). Two delay lines driven by sine LFOs 90° apart. When one LFO is at its turnaround (fastest modulation, most artifact-prone), the other is at its flattest — the two lines mask each other's reversal artifacts. Produces smooth, continuous chorus with no periodic click. CC 88 (depth) + 89 (rate) + 90 (mix), serial `cho d/r/m <0-127>`.

### CPU load indicator

Core 1 times each `tick()` call using the DWT cycle counter (240 MHz resolution, raw register access at `0xE0001004`). The accumulated compute time per buffer vs the 5.8ms budget gives true audio CPU load. Displayed as a coloured square in the top bar — green <50%, orange 50–79%, red ≥80%. If it stays orange/red, reduce `MAX_ACTIVE_VOICES` in `config.h`.

---

## USB Host

One USB MIDI device on the host socket (direct or through a hub). Two devices simultaneously through a hub are not reliably supported with the vendored `pio_usb` v0.5.3.

Practical workflow: keyboard on USB host, drawbar controller on USB device port (PC connection), scanner Pico on UART GP5.

---

## Known Limitations and Next Steps

- **Keyboard matrix scanner** — second Pico, UART MIDI to GP5. Next build phase.
- **USB hub multi-device** — see USB Host section above.
- **Leslie / rotary speaker** — not implemented. Suggested: use hardware Leslie unit.
- **Vibrato / chorus** — implemented. Leslie would add rotating speaker simulation on top.