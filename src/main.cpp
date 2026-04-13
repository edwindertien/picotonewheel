// ============================================================
//  main.cpp  –  Step 3a: Single-note MIDI → tone
//
//  MIDI input:
//    USB-MIDI  : plug Pico USB into host (class-compliant)
//    UART MIDI : GP5 (RX), 31250 baud, via optocoupler
//
//  Serial commands (115200 baud):
//    info   -> voice state
//    pio    -> PIO diagnostics
//    440    -> force 440 Hz tone (bypass MIDI, for testing)
//    0      -> silence
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "oscillator.h"
#include "voice.h"
#include "audio_driver.h"
#include "midi_handler.h"

int16_t sineTable[WAVETABLE_SIZE];

Voice voice;

void handleSerial();
void printInfo();

// ---- MIDI callbacks (called from midi_handler.h) -----------

void midi_note_on(uint8_t note, uint8_t velocity)
{
    voice.noteOn(note, velocity);
    Serial.print("Note ON  : "); Serial.print(note);
    Serial.print("  vel=");      Serial.print(velocity);
    Serial.print("  freq=");
    Serial.println(Voice::noteToFreq(note), 2);
}

void midi_note_off(uint8_t note)
{
    voice.noteOff(note);
    Serial.print("Note OFF : "); Serial.println(note);
}

void midi_pitch_bend(int16_t bend)
{
    voice.setPitchBend(bend);
    // (no serial print — too frequent during bend)
}

// ============================================================
void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // USB-MIDI init must come before Serial.begin() in TinyUSB mode
    midi_handler_init();

    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(200);

    Serial.println();
    Serial.println("=== Tonewheel Organ – Step 3a: MIDI ===");

    wavetable_init();
    Serial.println("Wavetable ready.");

    audio_driver_init(SAMPLE_RATE);
    Serial.println("Audio driver ready.");

    audio_driver_debug();

    Serial.println("MIDI ready (USB + UART GP5).");
    Serial.println("Commands: info | pio | <freq_hz> | 0");

    digitalWrite(LED_PIN, HIGH);
}

// ============================================================
void loop()
{
    // Poll MIDI (non-blocking)
    midi_handler_poll();

    // Handle serial debug commands
    handleSerial();

    // Fill audio buffer
    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = voice.tick();
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
                } else {
                    float freq = line.toFloat();
                    if (freq <= 0.0f) {
                        voice.osc.active = false;
                        Serial.println("Silence");
                    } else {
                        voice.osc.setFrequency(freq);
                        voice.osc.amplitude = 0.7f;
                        voice.osc.active    = true;
                        Serial.print("Force freq -> ");
                        Serial.print(freq, 2); Serial.println(" Hz");
                    }
                }
            }
            line = "";
        } else {
            line += c;
        }
    }
}

void printInfo()
{
    Serial.println("--- Voice ---");
    if (voice.currentNote != 255) {
        Serial.print("  Note      : "); Serial.println(voice.currentNote);
        Serial.print("  Frequency : ");
        float f = (float)voice.osc.phaseInc * SAMPLE_RATE / 4294967296.0f;
        Serial.print(f, 2); Serial.println(" Hz");
        Serial.print("  Amplitude : "); Serial.println(voice.osc.amplitude, 3);
        Serial.print("  PitchBend : "); Serial.print(voice.pitchBend, 3);
        Serial.println(" semitones");
    } else {
        Serial.println("  No note playing");
    }
    Serial.println("-------------");
}