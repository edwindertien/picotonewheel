# Project Context — Pico Tonewheel Organ (tone generator)

Technical handoff for developers and for new AI-assisted sessions. Read it together with `README.md`
(what it is and how to build it). This file is about **why it works the way it does and what will break if you change it.**

Status: all items below were confirmed working on the hardware by the author unless marked *(host-verified only)*.

---

## 1. What this is

Dual-unit Hammond-style organ. This repository is the **tone generator**: Raspberry Pi **Pico 2 (RP2350)**,
Waveshare Pico-Audio (CS4344, I²S) and Pico-LCD-1.14 (ST7789V). Input = MIDI from UART (GP5), USB host (GP6/GP7, one device)
and the native USB port. Companion: the *pico MIDI keyboard* unit (RP2040) that sends notes and CC12–27.

Toolchain: PlatformIO, `https://github.com/maxgerhardt/platform-raspberrypi.git`, earlephilhower core, Adafruit TinyUSB (3.7.x),
FortySevenEffects MIDI library, vendored Pico-PIO-USB v0.5.3 in `lib/pio_usb/`.

## 2. Current feature set

- 16-note (pool of 20) polyphonic tonewheel synthesis, 9 partials/note, drawbars, percussion (first-note-only), key click
- Effects chain: overdrive → chorus → **Leslie (stereo)**; vibrato inside the voices
- **Load governor** (adaptive polyphony with graceful fade-out) and `perf` console command
- Control by MIDI CC, joystick + LCD, serial console. CC map in section 6.
- USB MIDI host: **one** device (hub OK for one device)

## 3. Layout

```
platformio.ini            envs: pico2_tone (240 MHz, full) · pico2_nohost (200 MHz) · pico_tone (OUTDATED, see 9)
include/                  all headers (header-only DSP)
  config.h                every tunable + CC map + helper midi_switch3()
  tonewheel_manager.h     voice pool, spinlock, tick(), effect order, governor
  tonewheel_voice.h       one note: 9 partials, compact index list, tickRaw(), applyVibrato(), fade
  effects.h               Overdrive · Vibrato · Chorus · Leslie, _fx_sin()
  perf.h                  DWT cycle counter (raw registers, ARM-only; host stub)
  drawbars.h percussion.h click.h oscillator.h wavetable.h
  midi_handler.h usb_host.h audio_driver.h audio_pio.pio.h
  lcd*.h  (UI in src/lcd.cpp)     mclk.h voice.h voice_manager.h  = unused/reference
src/  main.cpp  lcd.cpp  audio_pio.pio
lib/pio_usb/              vendored + compatibility stub (section 8)
docs/                     figures; docs/src/make_figures.py regenerates them
```

## 4. Architecture

### 4.1 Cores and the shared organ
- **Core 0 `loop()`**: `midi_handler_poll` → `organ.serviceGovernor()` → `usb_host_poll` → `ui_handle_buttons` → LCD flush every 33 ms → `handleSerial` (1 char per pass). **No `delay()`** in `loop()`.
- **Core 1 `loop1()`** (`__not_in_flash_func`): per 256-sample buffer: for each sample `organ.tick(L,R)` then `audio_driver_put(L,R)` (interleaved), then `organ.governor(cycles, budget)`, then the LCD load value.
- One global `TonewheelManager organ`. Core 0 writes under `spin_lock_unsafe_blocking`; core 1 `spin_try_lock_unsafe` — it renders even if the lock is taken (worst case one slightly stale sample) and **never waits**.
- Parameters are `uint8_t`/enum single writes (atomic on ARM): no lock needed from core 0.

### 4.2 Hard rules (each cost hours; do not "simplify")
1. **Never `spin_lock_blocking()`** — disables interrupts → TinyUSB drops microframes → USB MIDI dies under chord bursts.
2. **Never double-pass audio** (compute buffer, then push). The PIO FIFO holds a few samples (~100–180 µs). A starved FIFO stops LRCK; the **CS4344 hardware-mutes** (shows up as noise whose pitch falls with load, then silence). Always interleave compute and put.
3. **Core 1 must not call**: `Serial`, LCD/SPI, LittleFS, NeoPixel `show()`. Core 1 must never block.
4. **Audio code runs from SRAM.** Only `loop1` was in RAM; everything it called was an ordinary function in *flash*, executed through the 16 KB XIP cache that core 0 (never idle) constantly evicts → large unpredictable stalls. Fix: `__attribute__((always_inline))` on `tick`, `Overdrive::process`, `Chorus::process`, `Leslie::process`, `tickRaw`, `_fx_sin`. Verify with `arm-none-eabi-g++ -S` that the audio path is one call-free function (currently ~1230 static instructions; calls only to cold `memset`/`sqrtf` error branches).
5. **Init order**: `audio_driver_init()` (claims PIO2) **before** `usb_host_init()` (TinyUSB takes PIO0 TX / PIO1 RX — 22 and 32 of 32 slots; PIO1 is exactly full).
6. **TinyUSB host callbacks must not make `_sync` calls** — blocks `USBHost.task()` and starves hub enumeration.
7. **No `float` in the per-oscillator path.** Hardware FPU is fine elsewhere, but the vibrato float convert/multiply per partial per sample was ~half the voice cost.
8. **DWT**: CMSIS `CoreDebug`/`DWT` are not exposed in arduino-pico. Use raw addresses `0xE000EDFC` (DEMCR bit 24), `0xE0001000` (CTRL bit 0), `0xE0001004` (CYCCNT). The DWT is **per core**: enable it on core 1.

### 4.3 PIO / clocks
RP2350 has 3 PIO blocks: PIO0 USB TX · PIO1 USB RX · PIO2 I²S (13 instr., the original Waveshare program, `.side_set 2`, **do not use the earlephilhower I²S library** — BCK/LRCK bit order is incompatible with the board wiring). 240 MHz gives exact PIO-USB divisors (TX ÷5, RX ÷2.5); the audio clkdiv is computed from `clock_get_hz()`.

## 5. DSP design notes

### 5.1 Voice engine (`tonewheel_voice.h`, `tonewheel_manager.h`)
- Partial = `{phase, inc, baseInc, amp, pad}` (16 bytes, one record per drawbar index) + compact list `_idx[]` of sounding partials (level>0, freq<22 kHz). `_idx` entries are always valid indices 0..8, so a racy read from core 1 can glitch one sample but never crash.
- `tickRaw()` returns the **unscaled** Σ(sine·amp); the manager applies `×559241 >> 32` (= ÷7680 = the old `>>8`, `/10`, `/3`). int32 headroom: 180 partials × 32767 × 256 < 2³¹.
- **Vibrato**: LFO runs every `VIBRATO_BLOCK`=16 samples; multiplier in **Q30** (`VIB_ONE = 1<<30`); `applyVibrato(q)` does `inc = baseInc·q >> 30` per sounding partial. New/retuned voices get the current state immediately (`setVibratoState`, `_updatePartials` re-applies). Q16 was *not* enough (phase error at high partials). Block-rate vs per-sample: same sound; noise floor below −93 dB.
- **Fade**: stolen voices fade over `VOICE_FADE_SAMPLES` (Q16 gain); `fading` voices are not "held" (`currentNote=255`, excluded from counts and from the duplicate-note check). A hard cut measured 24× the signal's own step (a click); the fade measures none.
- `_fx_sin(phase)` = parabola + refinement (error ≈ 0.001). **The previous polynomial was wrong at the ends of its range (±0.54 → a 1.08 step once per LFO cycle)** — this was the real cause of the long-chased "chorus loop-around click". Callers must pass phase ≥ −0.5 (truncation = floor).

### 5.2 Percussion / click
First-note-only trigger; 8′ drawbar −25 % while percussion is on; level tracks `drawbarSum`. Click level scale: CC84 → 0–255 internal; LCD/serial use 0–127 (a pre-existing inconsistency, harmless).

### 5.3 Chorus
Dual quadrature BBD: two 2048-sample delay lines, LFOs 90° apart (`sin`, `cos`), centre 20 ms, swing `depth·2/127` ms, rate 0.2–3 Hz, wet 0–50 % by `mix`. 5 ms swing (the first version) gave ±44 cents — far too much.

### 5.4 Overdrive (`Overdrive`, effects.h) — redesigned, measured
Signal path: split at 120 Hz (bass stays clean, delayed 7 samples) → gain 0…`OD_MAX_GAIN_DB` (linear in dB, `OD_GAIN_CURVE`=1.0) → **2× oversampling** (15-tap half-band, coefficients 0.313925991 / −0.093403660 / 0.044110376 / −0.026487629, polyphase, doubled ring buffers) → asymmetric cubic soft clipper (`c(u)=u−u³/3`, ±2/3; negative half `0.6·c(u/0.6)`) with **first-order ADAA** (antiderivative F, threshold |du|>2e-3 else midpoint) → half-band decimation → 2× one-pole LPF (16 kHz → 5 kHz with drive) → DC blocker (R=0.9986) → **loudness-following makeup** `m = k(drive)·√(Ex/Ey)` (detectors on the *distorted-band input* and the *processed band*, 20 ms attack / 5 ms release, updated every sample) → + clean bass. 10 ms on/off crossfade; parameters from a 128-entry table built in the constructor (no libm in the audio path).
- Why: a plain waveshaper at this drive **aliases at ≈ −30 dB** (inharmonic hash = "noise"); ADAA+2× brings it to ≈ −55 dB (ear-weighted ≈ −58…−61), the old effect's floor, with ~10 dB more distortion energy.
- Measured with: single note A4 (all partials harmonics of 220 Hz → anything between harmonics is aliasing), 5 test signals (1 quiet note … six-note full drawbars), A-weighted level vs clean.
- **Makeup calibration** (do it again if the clipper/filters/curve change): with 0 dB makeup the chain's A-weighted level vs clean was `0.089 − 1.163·s` dB, `s=(d/127)^1.3` (fit error ≤ 0.25 dB). So `k_dB = (OD_LEVEL_DB + 1.163)·s − 0.089`. Result: +0.1 dB at low drive → +2.6 dB at max (mean), all signals within ~2 dB.
- Rejected: updating the gain every 4 samples (sample-and-hold images → up to 7 dB worse aliasing at mid drive); gain > 36 dB (no extra clipping at max, more aliasing); fixed makeup (dense chords end up quieter than clean).
- Cost ≈ 250–300 cycles/sample, only while drive>0. Pitfalls seen: `1/g` compensation made it quieter; the asymmetric shaper alone leaves ~−100 LSB DC.

### 5.5 Leslie (`Leslie`, effects.h)
Crossover 800 Hz, **bilinear (TPT) one-poles**, 12 dB/oct each side, high band polarity-inverted so the bands sum to an allpass (exactly flat; the naive `high = x − low` leaked bass into the horn and a plain one-pole form drooped 1 dB at HF). Horn: delay swing ±`LESLIE_HORN_DOPPLER_MS`(0.5) (≈ ±2.2 % pitch at 6.7 Hz) + AM 60 %; drum: low band delayed 7 samples + AM 40 %. Two mics a quarter turn apart → stereo. `gain = (1−D/2) + (D/2)·cos θ`, `delay = base − A·cos θ`. Rotor speeds approach their targets exponentially (`ACCEL_S`/`DECEL_S` per rotor). Stop: when both rotors < `LESLIE_STOP_HZ`, the wet weight fades (0.4 s) and the effect **bypasses bit-exactly** (zero CPU). Makeup ×1.25 (level within ~1.5 dB of dry on typical registrations). Mode comes from `leslie.setSwitch(cc27)`; `LESLIE_POS_*` map the switch positions.

### 5.6 Load governor (`TonewheelManager::governor`, `serviceGovernor`)
Per 256-sample buffer on core 1: cycles in the voice loop vs. everything else → `_perOsc` (cycles/oscillator/sample) and `_rest` (cycles/sample), **rising fast (α 0.2), falling slowly (α 0.03)** (underestimating is the dangerous direction). Capacity = `(GOV_TARGET_LOAD_PCT%·budget − rest)/(perOsc·partialsPerNote)`, clamped to `[GOV_MIN_VOICES, MAX_ACTIVE_VOICES]`. `voiceLimit`: down at once; up +1 after `GOV_UP_BUFFERS` (~1 s) of spare capacity. **Emergency**: measured load > `GOV_EMERGENCY_PCT` → limit = held·target/load (proportional). Core 0 `serviceGovernor()` fades out held notes beyond the limit; `noteOn` steals the oldest held note the same way. Synthetic tests: converges in ~80 ms, no flapping, recovers; a heavy chord is overloaded for at most one buffer. `perf` prints the live numbers.
Context: before this, the engine (float vibrato on all partials, effects, everything in flash) overran at 6 notes (pitch −½ semitone) and froze at 7 (DAC mute).

## 6. MIDI map (config.h)

| CC | Meaning |
|----|---------|
| 12–20 | drawbars 1–9 |
| 21 vol · 22 drive · 23 chorus amount (depth+mix) | pots |
| 24 percussion · 25 vibrato · 26 click | three-state: 2 / 62 / 126 → `midi_switch3()` thresholds 32 / 94 |
| 27 | Leslie: low=slow, mid=stop, high=fast (`LESLIE_POS_*`) |
| 80–84 | percussion on/off, harmonic, decay, level; 84 click level |
| 86–90 | vibrato depth/rate, chorus depth/rate/mix |
| PC 0–3 | Full / Jazz / Flute / off |
`MIDI_CC_VOLUME` was 7, then 10; it is now **21**. `MIDI_CC_DRIVE` was 85; now **22** (85 unused). The keyboard unit's 16 analog controls = CC12–27 exactly.
Dispatch order in `midi_cc()`: print everything → volume → `handleFxCC` → `handleCC` (drawbars/perc/click).

## 7. Serial commands
`help info all db pre perc click vol drive vib d|r cho d|r|m|a les stop|slow|fast perf pio`. Every received CC is printed.

## 8. USB host facts and compatibility stub
- Needs the **latest** Adafruit TinyUSB (host MIDI class `midi_host.*` does not exist in 3.4.x). Callbacks (3.7.x): `tuh_midi_mount_cb(idx, const tuh_midi_mount_cb_t*)`, `tuh_midi_umount_cb(idx)`, `tuh_midi_rx_cb(idx, xferred)`, `tuh_midi_tx_cb(idx, xferred)`, `tuh_midi_packet_read(idx, uint8_t[4])`.
- `hcd_pio_usb.c` (inside TinyUSB) calls `pio_usb_host_endpoint_close()`, absent in v0.5.3 → stub (declaration in `pio_usb.h`, body at the end of `pio_usb_host.c`).
- 15 kΩ pull-down on D+ (GP6) = host mode. One device works; **two devices behind a hub enumerate but only the first gets its MIDI interface** — cause not isolated (hub/deferred-attach path, probably the old pio-usb). To investigate: `-DCFG_TUSB_DEBUG=2` with `SERIAL_TUSB_DEBUG Serial`; likely fix: use the Pico-PIO-USB bundled inside TinyUSB.
- AKAI MPK mini: needs to be present at power-up; its current draw can put noise on the audio — diode + 100 µF + 100 nF.
- Do not call `_sync` descriptor functions in `tuh_mount_cb`.

## 9. Known issues / not done
- Two USB devices through a hub (above).
- **`pico_tone` (RP2040) env is stale**: `perf.h` reads the M33 DWT (hard fault on M0+).
- Exact cycle costs unmeasured on hardware; numbers in the docs are static instruction-count estimates. Use `perf`.
- Velocity ignored (intentional). Pitch bend supported.
- Open question for docs: pin the TinyUSB version in `platformio.ini`.

## 10. How the DSP was verified (host harness + cross-compiler)
Not in the repo (yet). Recreate: compile the real headers on a PC with `g++ -std=gnu++17` against tiny stubs — `Arduino.h` (min/max/Serial no-ops), `hardware/sync.h` (`spin_lock_*` no-ops), and `wavetable.h`'s `sineTable`. `perf.h` already has a host fallback. Tests that exist:
- **CC mapping** (2/62/126 for all switches, pots, no collision with CC12–20)
- **Voice-engine equivalence** old vs new, same event script: ±3 LSB (vibrato off); with the LFO held identically ±14 LSB (−69 dB)
- **Leslie**: ramp timing, bit-exact bypass, Doppler/AM depth, L/R correlation, level vs dry on real organ signals, click-free mode switching
- **Overdrive**: Python prototype (`scipy`) for design, C++ float32 matches it (aliasing −57.4 vs −57.7 dB); A-weighted loudness over 5 signals × drives; aliasing via the "harmonics of 220 Hz" trick; edge cases (on/off switching, pot sweep, silence, extreme input)
- **Governor / fade**: synthetic cost plants (pessimistic, optimised, effects toggled, wrong model), hard-cut vs fade click test
- **`main.cpp`** compiled on the host with stubs and driven through `handleSerial()` (one char per call!) and `midi_cc()`
- **Cross-compile** `arm-none-eabi-g++ -mcpu=cortex-m33 -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -O2 -S` to inspect inlining, calls, loop instruction counts (oscillator loop ≈ 15 integer instructions; whole audio path one function).
Lessons: measure every "inaudible" optimisation (the 4-sample gain hold cost 5–7 dB of aliasing); a test must run the code the way the firmware does (e.g. `tick()` so fades finish).

## 11. Working agreement
Hardware-first, incremental: each change is tested on the instrument before the next. Deliver **only the changed files** (keeping `include/` vs `src/`); update README/context at verified breakpoints, not mid-debugging. When debugging goes in circles, revert to the last known-good state and rebuild incrementally. State uncertainty plainly.

## 12. Next ideas
- Companion keyboard unit: documentation + link in the README.
- Combined MIDI-in + USB-host board.
- Fix the hub / two-device case (newer pio-usb).
- Further modules on the same platform — **analog synth** (VCO/VCF/ADSR, Moog ladder: `s += g·(tanh(in) − tanh(s))` per stage; call `tanf` only on cutoff change), **FM**, **electric piano** (samples), **drum machine** (different main loop). Shared: `audio_driver.h`, `midi_handler.h`, `usb_host.h`, LCD drawing primitives, `oscillator.h`, `wavetable.h`, `perf.h`, the governor pattern and the effects (Leslie/chorus/overdrive are generic). Only voice engine, manager and UI differ.
- If CPU ever becomes limiting again: a **shared bank of 91 tonewheels** (fixed cost independent of the number of notes: per sample 91 table lookups + per-wheel weights updated on note on/off); it changes the partial tuning slightly to equal temperament and removes the need for voice stealing.
