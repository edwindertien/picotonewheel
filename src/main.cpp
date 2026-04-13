// ============================================================
//  main.cpp  –  Step 4: 9-drawbar tonewheel organ
//
//  Each key plays up to 9 harmonic partials simultaneously,
//  weighted by the 9 drawbar levels.
//  Drawbars controlled via MIDI CC 12-20.
//
//  Serial commands:
//    info         -> voice + drawbar state
//    pio          -> PIO diagnostics
//    all          -> panic (all notes off)
//    db 888000000 -> set drawbars (9 digits, 0-8 each)
//    pre full     -> preset: full organ
//    pre jazz     -> preset: jazz
//    pre flute    -> preset: flute
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "oscillator.h"
#include "tonewheel_manager.h"
#include "audio_driver.h"
#include "midi_handler.h"

int16_t sineTable[WAVETABLE_SIZE];

TonewheelManager organ;

void handleSerial();
void printInfo();

// ---- MIDI callbacks ----------------------------------------

void midi_note_on(uint8_t note, uint8_t velocity) {
    organ.noteOn(note, velocity);
    Serial.print("ON  n="); Serial.print(note);
    Serial.print(" ["); Serial.print(organ.activeCount());
    Serial.println("]");
}

void midi_note_off(uint8_t note) {
    organ.noteOff(note);
    Serial.print("OFF n="); Serial.print(note);
    Serial.print(" ["); Serial.print(organ.activeCount());
    Serial.println("]");
}

void midi_pitch_bend(int16_t bend) {
    organ.setPitchBend(bend);
}

void midi_cc(uint8_t cc, uint8_t value) {
    if (organ.handleCC(cc, value)) {
        Serial.print("DB CC"); Serial.print(cc);
        Serial.print("="); Serial.print(value);
        Serial.print("  ");
        organ.drawbars.debugPrint();
    }
}

// ============================================================
void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    midi_handler_init();

    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(200);

    Serial.println();
    Serial.println("=== Tonewheel Organ – Step 4: Drawbars ===");

    wavetable_init();
    organ.init();
    audio_driver_init(SAMPLE_RATE);

    audio_driver_debug();
    organ.drawbars.debugPrint();
    Serial.println("Commands: info | all | db <9digits> | pre full/jazz/flute");
    digitalWrite(LED_PIN, HIGH);
}

// ============================================================
void loop()
{
    midi_handler_poll();
    handleSerial();

    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = organ.tick();
        audio_driver_put(s, s);
    }
}

// ============================================================
void handleSerial()
{
    static String line = "";
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length() > 0) {
                if (line == "info") {
                    printInfo();
                } else if (line == "pio") {
                    audio_driver_debug();
                } else if (line == "all") {
                    organ.allNotesOff();
                    Serial.println("All notes off.");
                } else if (line.startsWith("db ") && line.length() == 12) {
                    // "db 888000000"
                    String db = line.substring(3);
                    for (int i = 0; i < NUM_DRAWBARS; i++) {
                        uint8_t v = db[i] - '0';
                        if (v <= 8) organ.drawbars.level[i] = v;
                    }
                    // Propagate to active voices
                    organ.propagateDrawbars();
                    Serial.print("Drawbars set: ");
                    organ.drawbars.debugPrint();
                } else if (line == "pre full") {
                    organ.drawbars.presetFullOrgan();
                    Serial.println("Preset: full organ");
                } else if (line == "pre jazz") {
                    organ.drawbars.presetJazz();
                    Serial.println("Preset: jazz");
                } else if (line == "pre flute") {
                    organ.drawbars.presetFlute();
                    Serial.println("Preset: flute");
                }
            }
            line = "";
        } else {
            line += c;
        }
    }
}

void printInfo() {
    Serial.println("--- Organ state ---");
    organ.debugPrint();
    Serial.println("-------------------");
}