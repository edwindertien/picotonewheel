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
    audio_driver_init(SAMPLE_RATE);

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

    delay(1);
}

// ============================================================
//  Core 1  —  audio, runs from SRAM
// ============================================================
void setup1() { delay(500); }

void __not_in_flash_func(loop1)() {
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = organ.tick();
        audio_driver_put(s, s);
    }
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