// ============================================================
//  main.cpp  –  Step 2: Single oscillator -> PIO I2S -> CS4344
//
//  Serial commands (115200 baud, send with newline):
//    440        -> set frequency in Hz
//    0          -> silence
//    vol 0.5    -> amplitude 0.0..1.0
//    info       -> oscillator settings
//    pio        -> PIO register diagnostics
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "wavetable.h"
#include "oscillator.h"
#include "audio_driver.h"

int16_t sineTable[WAVETABLE_SIZE];

Oscillator osc;

void handleSerial();
void printInfo();

// ============================================================
void setup()
{
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(200);

    Serial.println();
    Serial.println("=== Tonewheel Organ – Step 2 ===");

    Serial.println("Initialising wavetable...");
    wavetable_init();
    Serial.println("  done.");

    Serial.println("Initialising oscillator...");
    osc.setFrequency(440.0f);
    osc.amplitude = 0.7f;
    osc.active    = true;
    Serial.println("  done.");

    Serial.println("Initialising PIO audio driver...");
    audio_driver_init(SAMPLE_RATE);
    Serial.println("  done.");

    audio_driver_debug();
    printInfo();
    Serial.println("Commands: <freq_hz> | vol <0..1> | info | pio");

    digitalWrite(LED_PIN, HIGH);
}

// ============================================================
void loop()
{
    handleSerial();

    for (int i = 0; i < BUFFER_FRAMES; i++) {
        int16_t s = osc.tick();
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
                } else if (line.startsWith("vol ")) {
                    float v = constrain(line.substring(4).toFloat(), 0.0f, 1.0f);
                    osc.amplitude = v;
                    Serial.print("Volume -> "); Serial.println(v, 3);
                } else {
                    float freq = line.toFloat();
                    if (freq <= 0.0f) {
                        osc.active = false;
                        Serial.println("Silence");
                    } else {
                        osc.setFrequency(freq);
                        osc.active = true;
                        Serial.print("Frequency -> ");
                        Serial.print(freq, 2);
                        Serial.println(" Hz");
                    }
                }
            }
            line = "";
        } else {
            line += c;
        }
    }
}

// ============================================================
void printInfo()
{
    Serial.println("--- Oscillator ---");
    if (osc.active) {
        float freq = (float)osc.phaseInc * SAMPLE_RATE / 4294967296.0f;
        Serial.print("  Frequency : "); Serial.print(freq, 2); Serial.println(" Hz");
        Serial.print("  Amplitude : "); Serial.println(osc.amplitude, 3);
        Serial.print("  PhaseInc  : "); Serial.println(osc.phaseInc);
    } else {
        Serial.println("  silent");
    }
    Serial.println("------------------");
}