#pragma once
// ============================================================
//  perf.h  —  Cycle counter (DWT CYCCNT) for load measurement
//
//  The CMSIS CoreDebug/DWT structs are not exposed in the arduino-pico
//  build, so the Cortex-M33 registers are accessed directly.
//  NOTE: the DWT is per core — perf_enable() must be called on the core
//  that reads the counter (core 1, the audio core).
// ============================================================
#include <stdint.h>

#if defined(__arm__)
#define PERF_DEMCR   (*((volatile uint32_t*)0xE000EDFC))   // bit 24 = TRCENA
#define PERF_CTRL    (*((volatile uint32_t*)0xE0001000))   // bit 0  = CYCCNTENA
#define PERF_CYCCNT  (*((volatile uint32_t*)0xE0001004))

static inline void perf_enable() {
    PERF_DEMCR |= 0x01000000u;
    PERF_CTRL  |= 0x00000001u;
}
static inline uint32_t perf_now() { return PERF_CYCCNT; }
#else
// Host build (unit tests on a PC): fake monotonic counter
static inline void perf_enable() {}
static inline uint32_t perf_now() { static uint32_t c = 0; return c += 3; }
#endif

// CPU clock for budget calculations (arduino-pico defines F_CPU from board_build.f_cpu)
#ifndef F_CPU
#define F_CPU 240000000L
#endif