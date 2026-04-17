# Pico Tonewheel Organ — Tone Generator (USB Host build)

A Hammond-style tonewheel organ tone generator built on the Raspberry Pi Pico 2 (RP2350), using a Waveshare Pico-Audio Rev 2.1 DAC and Waveshare Pico-LCD-1.14 display. Receives MIDI from three simultaneous sources and merges them into the synthesis engine:

- **USB device** (Pico's native USB port) — PC, DAW, or USB MIDI controller
- **USB host** (GP6/GP7 socket) — one USB MIDI keyboard or controller
- **UART** (GP5) — keyboard matrix scanner Pico (next build phase)

This is the **tone generator board** in a two-Pico system. The companion keyboard matrix scanner sends MIDI to this board over UART. The tone generator can also be driven by any external MIDI source via USB or UART.

---

## Hardware

### Stacking Order

```
[Pico-LCD-1.14]   ← top
[Pico-Audio]      ← middle  (pass-through header exposes all Pico GPIO)
[Pico / Pico 2]   ← bottom
```

### Pin Assignments

**Pico-Audio Rev 2.1 (I2S — hard-wired on PCB):**

| Signal | GPIO | Notes |
|--------|------|-------|
| I2S DIN | GP22 | Audio data |
| I2S BCK | GP26 | Bit clock |
| I2S LRCK | GP27 | Word select |

Audio uses **PIO2** (not PIO0) to leave PIO0 and PIO1 free for USB host.

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

USB-A connector pinout (looking into socket, left to right): VBUS(red) · D−(white) · D+(green) · GND(black).

**MIDI UART (hardware MIDI-in via optocoupler):**

| Signal | GPIO |
|--------|------|
| UART RX | GP5 |

---

## Building

### Requirements

- VSCode + PlatformIO extension
- earlephilhower Arduino-Pico core (installed automatically)

### Environments

```
pico2_tone    — Pico 2 (RP2350, 240 MHz) — full build with USB host
pico2_nohost  — Pico 2 (RP2350, 200 MHz) — no USB host (audio/MIDI test)
pico_tone     — Pico 1 (RP2040, 133 MHz) — no USB host
```

240 MHz is used for the USB host build because it gives exact PIO clock divisors for USB full-speed timing (USB TX ÷5, RX ÷2.5). Audio pitch is unaffected — the I2S clkdiv is calculated at runtime from the actual clock.

```bash
pio run -e pico2_tone --target upload
```

The vendored `lib/pio_usb/` library must be present. It provides the PIO-based USB host hardware layer used by TinyUSB.

---

## Configuration

All configurable values are in `src/config.h`.

### Feature flags

```cpp
#define ENABLE_LCD        1
#define ENABLE_SERIAL     1
#define ENABLE_USB_HOST   1   // 0 = disable USB host socket
```

### USB host pin

```cpp
#define MIDI_HOST_DP_PIN  6   // D+ on GP6, D- automatically on GP7
```

### Polyphony, MIDI channel, MIDI CC mapping

Same as the `tonewheel_p2` build — see that README for the full table.

---

## PIO Resource Layout

The RP2350 has three PIO blocks. This project uses all three:

| PIO | Used for | Programs | Instructions |
|-----|----------|----------|-------------|
| PIO0 | USB host TX | `usb_tx_dpdm` | 22 / 32 |
| PIO1 | USB host RX | `usb_edge_detector` + `usb_nrzi_decoder` | 32 / 32 |
| PIO2 | Audio I2S | `audio_pio` | 13 / 32 |

`audio_driver_init()` must be called before `usb_host_init()` so PIO2 is claimed first, leaving PIO0 and PIO1 for TinyUSB to use in the correct order.

---

## USB Host Notes

### What works
- One USB MIDI device plugged directly into the host socket
- One USB MIDI device plugged through a hub (the device connected first enumerates and works)
- Hot-plug and unplug — `allNotesOff()` is called on disconnect

### What doesn't work
- Two USB MIDI devices simultaneously through a hub — the second device enumerates at the USB level but TinyUSB's MIDI class driver only opens the first. This is a known limitation of the vendored `pio_usb` v0.5.3 library combined with the current TinyUSB hub driver's enumeration sequencing. It can be resolved in a future build by vendoring a newer version of `Pico-PIO-USB`.

### Practical workflow
- One device on the USB host socket (keyboard)
- One device on the USB device port / PC connection (drawbar controller or DAW)
- UART on GP5 for the keyboard matrix scanner Pico

### Compatibility stub
The vendored `lib/pio_usb/pio_usb_host.c` contains a stub for `pio_usb_host_endpoint_close()` which was added in a later version of Pico-PIO-USB. The Adafruit TinyUSB library's `hcd_pio_usb.c` calls it; the stub satisfies the linker without breaking functionality.

---

## Serial Commands

Connect at 115200 baud. Same command set as `tonewheel_p2`:

| Command | Effect |
|---------|--------|
| `help` | List all commands |
| `info` | Print organ state |
| `all` | All notes off |
| `db 888000000` | Set all 9 drawbars |
| `pre full\|jazz\|flute\|off` | Load preset |
| `perc on\|off\|2\|3\|fast\|slow\|soft\|norm` | Percussion controls |
| `click <0-127>` | Key click level |
| `vol <0-127>` | Master volume |
| `pio` | Audio PIO debug state |

---

## Display

Identical to `tonewheel_p2`. See that README for the full UI description.

**Joystick direction:** stick DOWN pulls the drawbar down (increases level, more amber). Stick UP pushes the bar up (decreases level).

---

## File Structure

```
platformio.ini          Build config (pico2_tone, pico2_nohost, pico_tone)

src/
  config.h              ALL configuration
  main.cpp              Core 0 + Core 1 entry points, MIDI callbacks, serial
  audio_driver.h        PIO I2S on PIO2
  audio_pio.pio/.pio.h  Waveshare I2S PIO program
  usb_host.h            USB MIDI host — init, poll, TinyUSB callbacks
  midi_handler.h        USB device MIDI + UART MIDI
  wavetable.h           Sine LUT
  oscillator.h          Phase-accumulator oscillator
  tonewheel_voice.h     9-oscillator voice (one per drawbar)
  tonewheel_manager.h   Polyphonic manager, spinlock, masterVolume
  drawbars.h            9-drawbar state and CC handling
  percussion.h          Hammond percussion
  click.h               Key click
  lcd.h / lcd.cpp       ST7789V display driver

lib/
  pio_usb/              Vendored Pico-PIO-USB v0.5.3 + compatibility stub
```

---

## Known Limitations and Next Steps

- **Keyboard matrix scanner** — the second Pico that scans the physical keyboard and sends MIDI over UART to GP5. Next build phase.
- **USB hub multi-device** — single device only on the host socket. See USB Host Notes above.
- **Leslie / rotary speaker** — not implemented.
- **Vibrato / chorus** — not implemented.