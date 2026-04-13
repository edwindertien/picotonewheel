// ============================================================
//  main.cpp  –  Step 3b: Polyphonic MIDI organ
//
//  Up to MAX_VOICES (16) simultaneous notes.
//  Organ behaviour: same note can't re-trigger while held.
//
//  MIDI input: USB-MIDI (class-compliant) + UART on GP5
//
//  Serial commands:
//    info   -> active voice list
//    pio    -> PIO diagnostics
//    all    -> panic: silence all voices
//    <hz>   -> force single tone (bypasses MIDI)
//    0      -> silence
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "oscillator.h"
#include "voice_manager.h"
#include "audio_driver.h"
#include "midi_handler.h"

int16_t sineTable[WAVETABLE_SIZE];

VoiceManager voices;

void handleSerial();
void printInfo();

// ---- MIDI callbacks ----------------------------------------

void midi_note_on(uint8_t note, uint8_t velocity)
{
    voices.noteOn(note, velocity);
    Serial.print("ON  n="); Serial.print(note);
    Serial.print(" v=");    Serial.print(velocity);
    Serial.print(" [");     Serial.print(voices.activeCount());
    Serial.println(" voices]");
}

void midi_note_off(uint8_t note)
{
    voices.noteOff(note);
    Serial.print("OFF n="); Serial.print(note);
    Serial.print(" [");     Serial.print(voices.activeCount());
    Serial.println(" voices]");
}

void midi_pitch_bend(int16_t bend)
{
    voices.setPitchBend(bend);
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
    Serial.println("=== Tonewheel Organ – Step 3b: Polyphony ===");
    Serial.print("Max voices: "); Serial.println(MAX_VOICES);

    wavetable_init();
    voices.init();
    audio_driver_init(SAMPLE_RATE);

    audio_driver_debug();
    Serial.println("Commands: info | pio | all | <freq_hz> | 0");
    digitalWrite(LED_PIN, HIGH);
}

// ============================================================
void loop()
{
    midi_handler_poll();
    handleSerial();

    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = voices.tick();
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
                    // MIDI panic
                    for (int n = 0; n < 128; n++) voices.noteOff(n);
                    Serial.println("All notes off.");
                } else {
                    float freq = line.toFloat();
                    if (freq <= 0.0f) {
                        for (int n = 0; n < 128; n++) voices.noteOff(n);
                        Serial.println("Silence");
                    } else {
                        // Force a single tone via MIDI note 69 (A4=440)
                        // mapped to requested freq by direct osc access
                        for (int n = 0; n < 128; n++) voices.noteOff(n);
                        voices.noteOn(69, 100);
                        Serial.print("Force -> "); Serial.print(freq, 2);
                        Serial.println(" Hz (use MIDI for exact pitch)");
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
    Serial.println("--- Voice state ---");
    voices.debugPrint();
    Serial.println("-------------------");
}