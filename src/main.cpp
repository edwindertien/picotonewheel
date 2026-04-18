// ============================================================
//  main.cpp  —  Tonewheel organ  (v10 clean)
//
//  Audio engine: v7 (known-good, untouched)
//  Display:      Waveshare Pico-LCD-1.14 (optional, ENABLE_LCD)
//  Serial:       command interface (optional, ENABLE_SERIAL)
//
//  All config in config.h — MIDI CCs, channel, feature flags.
//
//  Core 0: MIDI + LCD + serial
//  Core 1: audio  (loop1 in SRAM via __not_in_flash_func)
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "oscillator.h"
#include "tonewheel_manager.h"
#include "audio_driver.h"
#if ENABLE_LCD
#  include "lcd.h"
#endif
#include "midi_handler.h"
#if ENABLE_USB_HOST
#  include "usb_host.h"
#endif

int16_t sineTable[WAVETABLE_SIZE];
TonewheelManager organ;

#if ENABLE_SERIAL
static void handleSerial();
#endif

// ---- MIDI callbacks ----------------------------------------

void midi_note_on(uint8_t note, uint8_t velocity) {
#if MIDI_NOTE_PERC_TOGGLE >= 0
    if (note == MIDI_NOTE_PERC_TOGGLE) {
        organ.perc.enabled = !organ.perc.enabled;
        organ.propagateDrawbars();
#if ENABLE_LCD
        ui_mark_dirty_bottom();
#endif
        return;
    }
#endif
    organ.noteOn(note, velocity);
#if ENABLE_LCD
    ui_midi_activity();
    ui_mark_dirty_top();
#endif
#if ENABLE_SERIAL
    Serial.print("ON  n="); Serial.print(note);
    Serial.print(" ["); Serial.print(organ.activeCount()); Serial.println("]");
#endif
}

void midi_note_off(uint8_t note) {
#if MIDI_NOTE_PERC_TOGGLE >= 0
    if (note == MIDI_NOTE_PERC_TOGGLE) return;
#endif
    organ.noteOff(note);
#if ENABLE_LCD
    ui_mark_dirty_top();
#endif
#if ENABLE_SERIAL
    Serial.print("OFF n="); Serial.print(note);
    Serial.print(" ["); Serial.print(organ.activeCount()); Serial.println("]");
#endif
}

void midi_pitch_bend(int16_t bend) {
    organ.setPitchBend(bend);
}

void midi_cc(uint8_t cc, uint8_t value) {
    if (cc == MIDI_CC_VOLUME) {
        organ.masterVolume = (uint8_t)((uint16_t)value * 255 / 127);
#if ENABLE_LCD
        ui_mark_dirty_bottom();
#endif
#if ENABLE_SERIAL
        Serial.print("Vol: "); Serial.println(value);
#endif
        return;
    }
    if (organ.handleFxCC(cc, value)) {
#if ENABLE_LCD
        ui_mark_dirty_bottom();
#endif
#if ENABLE_SERIAL
        Serial.print("FX CC "); Serial.print(cc);
        Serial.print("="); Serial.println(value);
#endif
        return;
    }
    if (organ.handleCC(cc, value)) {
#if ENABLE_LCD
        ui_mark_dirty_bars();
        ui_mark_dirty_bottom();
        ui_mark_dirty_top();
#endif
#if ENABLE_SERIAL
        Serial.print("CC "); Serial.print(cc);
        Serial.print("="); Serial.println(value);
#endif
    }
}

void midi_program_change(uint8_t program) {
#if MIDI_PROGCHANGE_PRESETS
    switch (program % 4) {
        case 0: organ.drawbars.presetFullOrgan(); break;
        case 1: organ.drawbars.presetJazz();      break;
        case 2: organ.drawbars.presetFlute();     break;
        case 3: organ.drawbars.presetOff();       break;
    }
    organ.propagateDrawbars();
#if ENABLE_LCD
    ui_mark_all_dirty();
#endif
#if ENABLE_SERIAL
    Serial.print("PC "); Serial.println(program);
#endif
#endif
}

// ============================================================
//  Core 0  —  MIDI + display + serial
// ============================================================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    midi_handler_init();

#if ENABLE_SERIAL
    Serial.begin(115200);
    // No while(!Serial) — starts without CDC connection
    delay(100);
    Serial.println("=== Tonewheel Organ ===");
    Serial.print("LCD:    "); Serial.println(ENABLE_LCD    ? "ON" : "OFF");
    Serial.print("MIDI ch:"); Serial.println(MIDI_CHANNEL == 0 ? " OMNI" : String(MIDI_CHANNEL));
#endif

    wavetable_init();
    organ.init();
    audio_driver_init(SAMPLE_RATE);  // claims PIO2 first

#if ENABLE_USB_HOST
    // USB host init AFTER audio — PIO2 already claimed,
    // so TinyUSB will use PIO0 (TX) and PIO1 (RX) as intended
    usb_host_init();
#if ENABLE_SERIAL
    Serial.print("USB host: ON  D+=GP"); Serial.print(MIDI_HOST_DP_PIN);
    Serial.print(" D-=GP"); Serial.println(MIDI_HOST_DP_PIN + 1);
#endif
#endif

#if ENABLE_LCD
    lcd_begin();
    ui_mark_all_dirty();
#endif

#if ENABLE_SERIAL
    Serial.println("Ready. Type 'help' for commands.");
#endif

    digitalWrite(LED_PIN, HIGH);
}

static uint32_t _last_ui_ms = 0;

void loop() {
    midi_handler_poll();

#if ENABLE_USB_HOST
    usb_host_poll();
#endif

#if ENABLE_LCD
    ui_handle_buttons(organ);
    uint32_t now = millis();
    if (now - _last_ui_ms >= 33) {
        ui_flush(organ);
        _last_ui_ms = now;
    }
#endif

#if ENABLE_SERIAL
    handleSerial();
#endif
    // No delay() here — keep loop() spinning fast so MIDI events
    // are processed with minimum latency. LCD updates are rate-limited
    // by the 33ms timer above; serial is non-blocking.
}

// ============================================================
//  Core 1  —  audio, runs from SRAM
// ============================================================
// CPU load: percentage of audio budget used by tick() computation.
// Written by core 1, read by core 0 for display. uint8_t write is atomic.
volatile uint8_t g_cpu_load_pct = 0;

void setup1() { delay(500); }

void __not_in_flash_func(loop1)() {
    // Single-pass interleaved compute+put — PIO FIFO never starves.
    // Load measurement: time the entire compute loop using the cycle
    // counter (DWT CYCCNT, 1-cycle resolution at 240 MHz) then subtract
    // the known put() blocking time to get compute-only load.
    //
    // At 240 MHz, 1 µs = 240 cycles. time_us_32() has only 1µs resolution
    // which is too coarse for a single tick() call (~12µs).
    // Instead time the WHOLE 256-sample compute section accurately:
    // put() is isochronous (always takes BUDGET_US total), so:
    //   compute_us = elapsed_us - put_blocking_us
    // But we can't separate them in one pass.
    //
    // Simplest accurate approach: time the full loop, which always takes
    // ≈ BUDGET_US due to blocking puts. Track how often we finish EARLY
    // (FIFO accepts puts immediately) vs compute being tight.
    // A better proxy: count non-blocking put() calls. If FIFO has space
    // when we arrive, compute is ahead of the DAC — we have headroom.
    // 
    // Cleanest: use DWT cycle counter around just the tick() calls.
    // DWT is always available on Cortex-M33 (RP2350).

    static constexpr uint32_t BUDGET_US =
        (uint32_t)((1000000ULL * BUFFER_FRAMES) / SAMPLE_RATE);
    static constexpr uint32_t BUDGET_CY =
        (uint32_t)((uint64_t)240000000 * BUFFER_FRAMES / SAMPLE_RATE);

    // DWT cycle counter — direct register access using RP2350 addresses.
    // CMSIS CoreDebug/DWT structs are not exposed in the arduino-pico build;
    // use raw pointers instead (same registers, just no CMSIS wrapper).
    #define _DCB_DEMCR  (*((volatile uint32_t*)0xE000EDFC))
    #define _DWT_CYCCNT (*((volatile uint32_t*)0xE0001004))
    #define _DWT_CTRL   (*((volatile uint32_t*)0xE0001000))

    // Enable DWT cycle counter (idempotent — safe to call every buffer)
    _DCB_DEMCR |= 0x01000000u;  // TRCENA bit 24
    _DWT_CYCCNT = 0;
    _DWT_CTRL  |= 0x00000001u;  // CYCCNTENA bit 0

    // Time only tick() calls by accumulating per-sample cycle counts
    uint32_t total_cycles = 0;
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        uint32_t c0 = _DWT_CYCCNT;
        int16_t s = organ.tick();
        total_cycles += _DWT_CYCCNT - c0;
        audio_driver_put(s, s);
    }

    // load% = compute_cycles / budget_cycles * 100
    uint32_t load_now = (total_cycles * 100) / BUDGET_CY;

    // Fast-attack, fast-decay IIR — reacts quickly in both directions.
    // α_attack ≈ 0.5 (rises fast), α_decay ≈ 0.1 (falls in ~10 buffers)
    static uint32_t load_filt16 = 0;  // ×16 fixed-point
    uint32_t target = load_now * 16;
    if (target > load_filt16)
        load_filt16 = (load_filt16 * 1 + target * 15) / 16; // fast attack
    else
        load_filt16 = (load_filt16 * 13 + target * 3)  / 16; // fast decay

    g_cpu_load_pct = (uint8_t)((load_filt16 / 16) > 99 ? 99 : load_filt16 / 16);
}

// ============================================================
//  Serial commands
// ============================================================
#if ENABLE_SERIAL
static void handleSerial() {
    static String line = "";
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c != '\n' && c != '\r') { line += c; return; }
        line.trim();
        if (!line.length()) return;

        if (line == "help") {
            Serial.println("Commands:");
            Serial.println("  info              — organ state");
            Serial.println("  all               — all notes off");
            Serial.println("  db 888000000      — set drawbars (9 digits 0-8)");
            Serial.println("  pre full|jazz|flute|off");
            Serial.println("  perc on|off|2|3|fast|slow|soft|norm");
            Serial.println("  click <0-127>     — click level");
            Serial.println("  vol <0-127>       — master volume");
            Serial.println("  drive <0-127>     — overdrive (0=off)");
            Serial.println("  vib d <0-127>     — vibrato depth (0=off)");
            Serial.println("  vib r <0-127>     — vibrato rate");
            Serial.println("  cho d <0-127>     — chorus depth (0=off)");
            Serial.println("  cho r <0-127>     — chorus rate");
            Serial.println("  cho m <0-127>     — chorus wet/dry mix");
            Serial.println("  pio               — audio PIO state");
        } else if (line == "info") {
            organ.debugPrint();
        } else if (line == "pio") {
            audio_driver_debug();
        } else if (line == "all") {
            organ.allNotesOff(); Serial.println("All notes off.");
        } else if (line.startsWith("db ") && line.length() == 12) {
            for (int i = 0; i < NUM_DRAWBARS; i++) {
                uint8_t v = line[3 + i] - '0';
                if (v <= 8) organ.drawbars.level[i] = v;
            }
            organ.propagateDrawbars();
#if ENABLE_LCD
            ui_mark_all_dirty();
#endif
            Serial.print("Drawbars: "); organ.drawbars.debugPrint();
        } else if (line=="pre full")  { organ.drawbars.presetFullOrgan(); organ.propagateDrawbars(); Serial.println("Full organ"); }
          else if (line=="pre jazz")  { organ.drawbars.presetJazz();      organ.propagateDrawbars(); Serial.println("Jazz"); }
          else if (line=="pre flute") { organ.drawbars.presetFlute();     organ.propagateDrawbars(); Serial.println("Flute"); }
          else if (line=="pre off")   { organ.drawbars.presetOff();       organ.propagateDrawbars(); Serial.println("All off"); }
          else if (line=="perc on")   { organ.perc.enabled=true;  organ.propagateDrawbars(); Serial.println("Perc ON"); }
          else if (line=="perc off")  { organ.perc.enabled=false; organ.propagateDrawbars(); Serial.println("Perc OFF"); }
          else if (line=="perc 2")    { organ.perc.thirdHarmonic=false; Serial.println("2nd"); }
          else if (line=="perc 3")    { organ.perc.thirdHarmonic=true;  Serial.println("3rd"); }
          else if (line=="perc fast") { organ.perc.fast=true;  Serial.println("Fast"); }
          else if (line=="perc slow") { organ.perc.fast=false; Serial.println("Slow"); }
          else if (line=="perc soft") { organ.perc.soft=true;  Serial.println("Soft"); }
          else if (line=="perc norm") { organ.perc.soft=false; Serial.println("Norm"); }
          else if (line.startsWith("click ")) {
            organ.click.level = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Click: "); Serial.println(organ.click.level);
          } else if (line.startsWith("vol ")) {
            int v = constrain(line.substring(4).toInt(), 0, 127);
            organ.masterVolume = (uint8_t)((uint16_t)v * 255 / 127);
            Serial.print("Vol: "); Serial.println(v);
          } else if (line.startsWith("drive ")) {
            organ.overdrive.drive = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Drive: "); Serial.println(organ.overdrive.drive);
          } else if (line.startsWith("vib d ")) {
            organ.vibrato.depth = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Vib depth: "); Serial.println(organ.vibrato.depth);
          } else if (line.startsWith("vib r ")) {
            organ.vibrato.rate = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Vib rate: "); Serial.println(organ.vibrato.rate);
          } else if (line.startsWith("cho d ")) {
            organ.chorus.depth = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Chorus depth: "); Serial.println(organ.chorus.depth);
          } else if (line.startsWith("cho r ")) {
            organ.chorus.rate = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Chorus rate: "); Serial.println(organ.chorus.rate);
          } else if (line.startsWith("cho m ")) {
            organ.chorus.mix = constrain(line.substring(6).toInt(), 0, 127);
            Serial.print("Chorus mix: "); Serial.println(organ.chorus.mix);
          } else {
            Serial.print("Unknown: "); Serial.println(line);
          }

#if ENABLE_LCD
        ui_mark_dirty_bars(); ui_mark_dirty_bottom(); ui_mark_dirty_top();
#endif
        line = "";
    }
}
#endif