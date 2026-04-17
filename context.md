# Project Context — Pico Tonewheel Organ (USB Host build)

This is the handoff document for the `tonewheel_usb` build. Read alongside
`README.md` before making any changes. Contains architecture decisions,
hard-won lessons, known pitfalls, and the current state of every component.

This build adds a **USB MIDI host port** on GP6/GP7 to the `tonewheel_p2`
base. Everything in the `tonewheel_p2` CONTEXT.md still applies; this
document covers what changed and what was learned during the USB host work.

---

## What changed from tonewheel_p2

### 1. Audio moved from PIO0 to PIO2

The RP2350 has three PIO blocks. `tonewheel_p2` used PIO0 for I2S audio.
`Pico-PIO-USB` needs PIO0 (TX) and PIO1 (RX). Moving audio to PIO2 gives
each subsystem its own block with no conflicts.

**Change:** `audio_driver.h` line: `_pio = pio2;`
Also fixed `audio_driver_debug()` which had hardcoded `pio0_hw->` references
— replaced with `_pio->` throughout.

**Init order matters:** `audio_driver_init()` must be called before
`usb_host_init()`. PIO2 must be claimed first so TinyUSB's `hcd_init()`
takes PIO0 and PIO1 rather than conflicting with audio.

### 2. Clock: 200 MHz → 240 MHz

USB host requires exact PIO clock divisors. 240 MHz gives USB TX ÷5 and
RX ÷2.5 exactly. The audio I2S clkdiv is calculated at runtime via
`clock_get_hz()` so pitch is unaffected by the clock change.

### 3. New file: usb_host.h

Contains everything USB-host-related in one place:
- `Adafruit_USBH_Host USBHost` object
- `usb_host_init()` — configures PIO-USB on GP6/GP7, calls `USBHost.begin(1)`
- `usb_host_poll()` — calls `USBHost.task()`, called from `loop()`
- All four TinyUSB MIDI host callbacks with correct signatures for latest TinyUSB

### 4. New library: lib/pio_usb/

Vendored `Pico-PIO-USB` v0.5.3. Must be present in the project — TinyUSB's
`hcd_pio_usb.c` includes our `pio_usb.h` directly.

**Compatibility stub:** `pio_usb_host.c` has a stub appended at the end:
```cpp
bool pio_usb_host_endpoint_close(uint8_t root_idx, uint8_t device_address,
                                  uint8_t ep_addr) {
  (void)root_idx; (void)device_address; (void)ep_addr;
  return true;
}
```
The declaration is also in `pio_usb.h`. This satisfies the linker when using
Adafruit TinyUSB ≥3.5 which calls this function; v0.5.3 doesn't have it.

### 5. TinyUSB callback signatures

The latest Adafruit TinyUSB changed all MIDI host callback signatures.
The correct signatures (as of TinyUSB 3.7.x) are:

```cpp
void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data);
void tuh_midi_umount_cb(uint8_t idx);
void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes);
void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes);
bool tuh_midi_packet_read(uint8_t idx, uint8_t packet[4]);  // inline in midi_host.h
```

The parameter is `idx` (interface index), not `dev_addr`. Using the old
signatures causes a compile error with a clear message showing the expected
signature.

---

## USB host: what works and what doesn't

### Works reliably
- Single USB MIDI device on the host socket (direct or through a hub)
- Hot-plug/unplug with `allNotesOff()` on disconnect
- Simultaneous with USB device port (PC/DAW/controller on native USB,
  keyboard on host socket) — both merge into the engine transparently

### Does not work: two devices through a hub

Both devices enumerate at the USB level (`tuh_mount_cb` fires for both),
but only the first-connected device gets its MIDI class interface opened
(`tuh_midi_mount_cb` only fires once).

**Root cause investigated but not resolved.** The TinyUSB hub driver
processes one port's connection change per interrupt poll cycle and relies
on the next poll to pick up additional ports. With two devices already
connected at boot, the second port's change event should appear on the
subsequent hub status poll. The deferred attach queue (`_usbh_daq`,
sized to `CFG_TUH_HUB`) should handle this. Despite extensive investigation
including source-level tracing of the full enumeration state machine in
`usbh.c`, `hub.c`, and `midi_host.c`, the exact failure point was not
isolated without TinyUSB debug logging in place.

**To investigate further:** Add `-DCFG_TUSB_DEBUG=2` to build flags and
`#define SERIAL_TUSB_DEBUG Serial` before the TinyUSB include. The level-2
log prints every interface open/skip decision with class/subclass values.
This will show exactly which `TU_VERIFY` in `midih_open` fails for device 2.

**To fix properly:** Replace the vendored `lib/pio_usb/` with the version
bundled inside Adafruit TinyUSB's own source tree
(`src/portable/raspberrypi/Pico-PIO-USB/`). The bundled version is kept in
sync with TinyUSB's hub enumeration expectations; v0.5.3 predates several
hub-related fixes.

---

## Critical architecture rules (inherited from tonewheel_p2)

All rules from `tonewheel_p2` CONTEXT.md apply. Key ones:

**Spinlock pattern** — never use `spin_lock_blocking()`. Use:
- Core 0 (MIDI/USB): `spin_lock_unsafe_blocking()`
- Core 1 (audio tick): `spin_try_lock_unsafe()` — non-blocking

**Core split:**
- Core 0: MIDI device poll + USB host poll + LCD + serial
- Core 1: audio hot path (`__not_in_flash_func`) — runs from SRAM

**USB MIDI host callbacks must not make blocking calls.** The `tuh_mount_cb`
and `tuh_midi_mount_cb` callbacks are called from within `USBHost.task()`.
Any `_sync` descriptor fetch (e.g. `tuh_descriptor_get_device_sync()`) blocks
the USB task and can prevent other hub-connected devices from enumerating.
Keep all callbacks non-blocking.

---

## Hardware: USB-A socket wiring

```
GP6  ──── D+  (USB-A pin 3, green wire)  ──── 15kΩ pull-down to GND
GP7  ──── D-  (USB-A pin 2, white wire)
5V   ──── VBUS (USB-A pin 1, red wire)
GND  ──── GND  (USB-A pin 4, black wire)
```

The 15kΩ pull-down on D+ (GP6) is essential for host mode. Without it the
Pico appears as a device to anything plugged in and no enumeration occurs.
This is the same R13 issue documented in the USB2MIDI project context.

---

## Keyboard scanner — next phase

The second Pico will scan the physical organ keyboard matrix and send MIDI
to the tone generator over UART on GP5 at 31250 baud. The UART parser in
`midi_handler.h` is already wired and waiting.

The scanner Pico architecture:
- Scan diode-isolated key matrix (rows/columns)
- Debounce + key travel timing for velocity (dual-contact keys)
- Send note-on/note-off via UART MIDI at 31250 baud
- Optionally: also act as a USB MIDI host bridge for additional devices

See `tonewheel_p2/CONTEXT.md` for the full scanner design notes.

---

## File checklist

All files that must be present in the project:

```
platformio.ini
README.md
CONTEXT.md

src/
  config.h
  main.cpp
  audio_driver.h          ← uses PIO2 (changed from tonewheel_p2)
  audio_pio.pio
  audio_pio.pio.h
  usb_host.h              ← new in this build
  midi_handler.h
  wavetable.h
  oscillator.h
  tonewheel_voice.h
  tonewheel_manager.h
  drawbars.h
  percussion.h
  click.h
  lcd.h
  lcd.cpp
  voice.h                 ← reference only, not used
  voice_manager.h         ← reference only, not used

lib/
  pio_usb/
    library.json
    pio_usb.h             ← has endpoint_close declaration
    pio_usb.c
    pio_usb_host.c        ← has endpoint_close stub at end
    pio_usb_device.c
    pio_usb_ll.h
    pio_usb_configuration.h
    usb_crc.c / .h
    usb_definitions.h
    usb_rx.pio / .pio.h
    usb_tx.pio / .pio.h
```