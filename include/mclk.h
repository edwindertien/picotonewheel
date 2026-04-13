#pragma once
// ============================================================
//  mclk.h  –  Master clock (MCLK) generator for CS4344
//
//  The CS4344 on the Waveshare Pico-Audio Rev 2.1 requires an
//  MCLK input.  The CS4344 datasheet specifies:
//    MCLK = 256 × fs  (minimum), also accepts 384×, 512×
//
//  At fs = 44100 Hz:  MCLK = 256 × 44100 = 11.2896 MHz
//
//  We generate this with a one-instruction PIO program that
//  toggles a pin at half the desired frequency (PIO clkdiv).
//  The RP2040 system clock is 125 MHz by default.
//
//  PIO toggle period = 2 PIO cycles (one SET high, one SET low)
//  → PIO cycle frequency = MCLK target
//  → clkdiv = SYS_CLK / MCLK = 125000000 / 11289600 ≈ 11.075
//
//  We use the nearest achievable: clkdiv = 11  (11.36 MHz, <1% error)
//  The CS4344 PLL locks over a wide range so this is fine.
// ============================================================

#include <hardware/pio.h>
#include <hardware/clocks.h>
#include "config.h"

// Minimal PIO program: repeatedly set pin high then low
// Each instruction takes 1 PIO clock cycle.
static const uint16_t mclk_pio_program_instructions[] = {
    0xe001,  // SET PINS, 1   (pin high)
    0xe000,  // SET PINS, 0   (pin low)
};

static const struct pio_program mclk_pio_program = {
    .instructions = mclk_pio_program_instructions,
    .length       = 2,
    .origin       = -1,  // any offset
};

static PIO  mclk_pio = pio1;   // use PIO1, leaving PIO0 free for I2S
static uint mclk_sm  = 0;
static uint mclk_offset;

inline void mclk_init()
{
    mclk_offset = pio_add_program(mclk_pio, &mclk_pio_program);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, mclk_offset, mclk_offset + 1);

    // Route the SET base to our MCLK pin
    sm_config_set_set_pins(&c, I2S_MCLK_PIN, 1);

    // clkdiv: SYS_CLK / (2 × MCLK_target)
    // factor 2 because the loop is 2 instructions = one full toggle
    float sys_clk  = (float)clock_get_hz(clk_sys);
    float mclk_target = 256.0f * SAMPLE_RATE;          // 11.2896 MHz
    float clkdiv   = sys_clk / (2.0f * mclk_target);   // ≈ 5.54
    sm_config_set_clkdiv(&c, clkdiv);

    // Configure the MCLK GPIO as PIO output
    pio_gpio_init(mclk_pio, I2S_MCLK_PIN);
    pio_sm_set_consecutive_pindirs(mclk_pio, mclk_sm, I2S_MCLK_PIN, 1, true);

    pio_sm_init(mclk_pio, mclk_sm, mclk_offset, &c);
    pio_sm_set_enabled(mclk_pio, mclk_sm, true);
}