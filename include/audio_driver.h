#pragma once
// ============================================================
//  audio_driver.h  –  Waveshare Pico-Audio Rev 2.1 PIO driver
//
//  Uses original Waveshare PIO program (side_set 2):
//    GP22 = DIN   (data)
//    GP26 = BCK   (bit clock,   side-set bit 0)
//    GP27 = LRCK  (word select, side-set bit 1)
//    GP28 = not used by PIO
//
//  Regenerate audio_pio.pio.h with:
//    pioasm -o c-sdk src/audio_pio.pio src/audio_pio.pio.h
// ============================================================

#include <Arduino.h>
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include "audio_pio.pio.h"
#include "config.h"

static PIO  _pio;
static uint _sm;
static uint _offset;

inline void audio_driver_init(uint32_t sample_freq)
{
    _pio    = pio0;
    _sm     = pio_claim_unused_sm(_pio, true);
    _offset = pio_add_program(_pio, &audio_pio_program);

    audio_pio_init(_pio, _sm, _offset,
                   AUDIO_DATA_PIN,
                   AUDIO_CLOCK_PIN_BASE,
                   sample_freq);
}

// Blocking: stalls until FIFO has space. Normal operation.
inline void audio_driver_put(int16_t left, int16_t right)
{
    // FIFO word: bits 31..16 = left (ws=0), bits 15..0 = right (ws=1)
    uint32_t word = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
    pio_sm_put_blocking(_pio, _sm, word);
}

inline void audio_driver_debug()
{
    uint32_t sys_clk    = clock_get_hz(clk_sys);
    uint32_t clkdiv_reg = pio0_hw->sm[_sm].clkdiv;
    float    divf       = (float)(clkdiv_reg >> 16) +
                          (float)((clkdiv_reg >> 8) & 0xff) / 256.0f;
    // Original program: 128 instructions/frame, BCK=PIO_rate/2
    float    fs_actual  = (float)sys_clk / divf / 128.0f;
    float    bck_khz    = (float)sys_clk / divf / 2.0f / 1000.0f;
    bool     enabled    = (pio0_hw->ctrl >> _sm) & 1u;
    uint     pc         = pio0_hw->sm[_sm].addr & 0x1f;

    Serial.println("--- PIO debug ---");
    Serial.print("  PIO            : pio"); Serial.println(_pio == pio0 ? "0" : "1");
    Serial.print("  SM             : ");    Serial.println(_sm);
    Serial.print("  Prog offset    : ");    Serial.println(_offset);
    Serial.print("  entry_point at : ");    Serial.println(_offset + audio_pio_offset_entry_point);
    Serial.print("  PC now         : ");    Serial.println(pc);
    Serial.print("  sys_clk        : ");    Serial.print(sys_clk / 1000000); Serial.println(" MHz");
    Serial.print("  clkdiv reg     : 0x"); Serial.print(clkdiv_reg, HEX);
    Serial.print("  (int="); Serial.print(clkdiv_reg >> 16);
    Serial.print(" frac="); Serial.print((clkdiv_reg >> 8) & 0xff); Serial.println(")");
    Serial.print("  BCK            : ");    Serial.print(bck_khz, 1); Serial.println(" kHz (expect 2822.4)");
    Serial.print("  LRCK (fs)      : ");    Serial.print(fs_actual, 1); Serial.println(" Hz  (expect 44100)");
    Serial.print("  SM enabled     : ");    Serial.println(enabled ? "YES" : "NO  <-- problem!");
    Serial.print("  TX FIFO level  : ");    Serial.println(pio_sm_get_tx_fifo_level(_pio, _sm));
    Serial.print("  TX FIFO full   : ");    Serial.println(pio_sm_is_tx_fifo_full(_pio, _sm) ? "YES <-- SM stuck!" : "no");
    Serial.print("  Data pin       : GP"); Serial.println(AUDIO_DATA_PIN);
    Serial.print("  BCK  pin       : GP"); Serial.println(AUDIO_CLOCK_PIN_BASE);
    Serial.print("  LRCK pin       : GP"); Serial.println(AUDIO_CLOCK_PIN_BASE + 1);
    Serial.println("-----------------");
}