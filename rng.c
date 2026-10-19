/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * MT8167 TRNG register interface; see CREDITS.md. Never emit seed bytes.
 */
#include "handoff.h"
static volatile uint32_t *const rng = (void *)(uintptr_t)0x1020c000;
static uint64_t ticks(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}
int mt8167_random_seed(unsigned char seed[64], uint32_t timebase)
{
    volatile uint32_t *const clocks = (void *)(uintptr_t)0x10000000;
    uint32_t gated = clocks[0x3c / 4] & (1u << 5);
    if (gated) clocks[0x9c / 4] = 1u << 5;
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t control = rng[0];
    int result = 0;
    if (control == 0xffffffffu) { result = -1; goto restore_clock; }
    rng[0] = control | 1u;
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t previous = 0;
    for (unsigned i = 0; i < 16; i++) {
        uint64_t start = ticks();
        unsigned attempts = 1000000;
        while (!(rng[0] & (1u << 31))) {
            if (!--attempts || ticks() - start > timebase / 100) {
                result = -2; goto restore_rng;
            }
        }
        uint32_t value = rng[2];
        /* Basic stuck-source rejection, not a statistical certification. */
        if (!value || value == 0xffffffffu || (i && value == previous)) {
            result = -3; goto restore_rng;
        }
        previous = value;
        for (unsigned j = 0; j < 4; j++) seed[i * 4 + j] = (unsigned char)(value >> (j * 8));
    }
restore_rng:
    if (!(control & 1u)) rng[0] = control & ~1u;
restore_clock:
    if (gated) clocks[0x6c / 4] = 1u << 5;
    __asm__ volatile("dsb sy" ::: "memory");
    if (result) for (unsigned i = 0; i < 64; i++) ((volatile unsigned char *)seed)[i] = 0;
    return result;
}
