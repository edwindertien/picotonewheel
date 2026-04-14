// ============================================================
//  main.cpp  –  Step 5: Dual-core tonewheel organ
//
//  Core 0: MIDI polling, serial commands, drawbar updates
//  Core 1: audio buffer fill + PIO push (time-critical)
//
//  The two cores share the TonewheelManager.
//  Access is safe because:
//    - noteOn/noteOff/drawbar updates happen at MIDI rate (~1 kHz max)
//    - audio tick() reads oscillator state; the only write is phase++
//    - RP2040 has no cache coherency issues on shared SRAM
//    - A note parameter change mid-buffer causes at most 1 glitchy
//      sample — inaudible at 44100 Hz
//  A mutex would add latency on the audio core; we accept the
//  benign race condition here (same approach used in most embedded synths).
//
//  Serial commands:
//    info         -> voice + drawbar state
//    pio          -> PIO diagnostics
//    all          -> all notes off (panic)
//    db 888000000 -> set drawbars directly
//    pre full | jazz | flute -> presets
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

// ---- forward declarations ----------------------------------
void handleSerial();
void printInfo();

// ---- MIDI callbacks (core 0) -------------------------------

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
        Serial.print("DB  ");
        organ.drawbars.debugPrint();
    }
}

// ============================================================
//  Core 0 – setup / loop
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
    Serial.println("=== Tonewheel Organ – dual core ===");

    wavetable_init();
    organ.init();

    // Audio driver is initialised on core 0 but driven from core 1.
    // PIO and DMA are accessible from both cores.
    audio_driver_init(SAMPLE_RATE);
    audio_driver_debug();

    organ.drawbars.debugPrint();
    Serial.println("Commands: info | all | db <9digits> | pre full/jazz/flute");

    digitalWrite(LED_PIN, HIGH);
}

void loop()
{
    // Core 0: handle MIDI and serial at leisure
    midi_handler_poll();
    handleSerial();
    // Small yield so core 0 doesn't starve USB stack
    delay(1);
}

// ============================================================
//  Core 1 – audio engine
//  setup1() and loop1() run on the second RP2040 core.
// ============================================================
void setup1()
{
    // Wait until core 0 has initialised everything
    delay(500);
}

void loop1()
{
    // Fill one buffer's worth of samples and push to PIO.
    // audio_driver_put() blocks when the 8-word FIFO is full,
    // which naturally paces this loop to exactly sample rate.
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = organ.tick();
        audio_driver_put(s, s);
    }
}

// ============================================================
//  Serial command handler
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
                    String db = line.substring(3);
                    for (int i = 0; i < NUM_DRAWBARS; i++) {
                        uint8_t v = db[i] - '0';
                        if (v <= 8) organ.drawbars.level[i] = v;
                    }
                    organ.propagateDrawbars();
                    Serial.print("Drawbars: ");
                    organ.drawbars.debugPrint();
                } else if (line == "pre full") {
                    organ.drawbars.presetFullOrgan();
                    organ.propagateDrawbars();
                    Serial.println("Preset: full organ");
                } else if (line == "pre jazz") {
                    organ.drawbars.presetJazz();
                    organ.propagateDrawbars();
                    Serial.println("Preset: jazz");
                } else if (line == "pre flute") {
                    organ.drawbars.presetFlute();
                    organ.propagateDrawbars();
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