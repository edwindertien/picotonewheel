#pragma once
// ============================================================
//  config.h  –  Hardware & audio constants
//  Waveshare Pico-Audio Rev 2.1  +  RP2040
// ============================================================

// ---- I2S pin assignment (hard-wired on Rev 2.1 PCB) --------
//  GP22 = DIN   (audio data)
//  GP26 = BCK   (bit clock   — side-set bit 0)
//  GP27 = LRCK  (word select — side-set bit 1)
//  GP28 = not driven by PIO (leave floating / ignore)
//
//  AUDIO_CLOCK_PIN_BASE is the side-set base = GP26 (BCK).
//  Only 2 consecutive pins are used by the PIO program.
#define AUDIO_DATA_PIN        22
#define AUDIO_CLOCK_PIN_BASE  26   // BCK=GP26, LRCK=GP27

// ---- Audio parameters --------------------------------------
#define SAMPLE_RATE    44100

// Frames pushed per loop() iteration.
// At 44100 Hz, 256 frames ≈ 5.8 ms per batch.
#define BUFFER_FRAMES  256

// ---- Wavetable ---------------------------------------------
#define WAVETABLE_BITS 10
#define WAVETABLE_SIZE (1 << WAVETABLE_BITS)   // 1024 points

// ---- Phase accumulator ------------------------------------
#define PHASE_SHIFT    (32 - WAVETABLE_BITS)

// ---- Misc --------------------------------------------------
#define LED_PIN        25