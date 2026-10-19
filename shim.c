/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * Diagnostic stage only: no kernel launch, storage access or power changes.
 */
#include <stdint.h>

#define READ_SYSREG(reg) ({ uint64_t v; __asm__ volatile("mrs %0, " #reg : "=r"(v)); v; })
#define UART_BASE ((uintptr_t)0x11005000)
#define UART_LSR (UART_BASE + 0x14)
#define UART_TX_READY (1u << 5)

static void putc_uart(char c)
{
    /* Preserve the UART setup inherited from LK, including its baud rate. */
    for (unsigned int n = 0; n < 1000000; n++) {
        if (*(volatile uint32_t *)UART_LSR & UART_TX_READY) {
            *(volatile uint32_t *)UART_BASE = (uint8_t)c;
            return;
        }
    }
}

static void puts_uart(const char *s)
{
    while (*s) {
        if (*s == '\n') putc_uart('\r');
        putc_uart(*s++);
    }
}

static void value(const char *label, uint64_t v)
{
    puts_uart(label);
    puts_uart("=0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)(v >> shift) & 15;
        putc_uart((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
    }
    puts_uart("\n");
}

__attribute__((noreturn)) static void stop(void)
{
    for (;;) __asm__ volatile("wfe");
}

__attribute__((noreturn)) void exception_report(uint64_t slot)
{
    uint64_t el = READ_SYSREG(CurrentEL);
    puts_uart("XNU_SHIM_EXCEPTION\n");
    value("vector", slot);
    value("CurrentEL", el);
    if (el == 8) {
        value("ESR_EL2", READ_SYSREG(ESR_EL2));
        value("ELR_EL2", READ_SYSREG(ELR_EL2));
        value("FAR_EL2", READ_SYSREG(FAR_EL2));
    } else {
        value("ESR_EL1", READ_SYSREG(ESR_EL1));
        value("ELR_EL1", READ_SYSREG(ELR_EL1));
        value("FAR_EL1", READ_SYSREG(FAR_EL1));
    }
    stop();
}

__attribute__((noreturn)) void shim_main(uint64_t x0, uint64_t x1,
                                        uint64_t x2, uint64_t x3, uint64_t old_sp)
{
    uint64_t el = READ_SYSREG(CurrentEL);
    puts_uart("\nXNU_A04BR3_SHIM_DIAGNOSTIC_V1\n");
    value("CurrentEL", el);
    value("entry_x0", x0);
    value("entry_x1", x1);
    value("entry_x2", x2);
    value("entry_x3", x3);
    value("entry_sp", old_sp);
    value("MIDR_EL1", READ_SYSREG(MIDR_EL1));
    value("MPIDR_EL1", READ_SYSREG(MPIDR_EL1));
    value("ID_AA64ISAR0_EL1", READ_SYSREG(ID_AA64ISAR0_EL1));
    value("ID_AA64ISAR1_EL1", READ_SYSREG(ID_AA64ISAR1_EL1));
    value("ID_AA64PFR0_EL1", READ_SYSREG(ID_AA64PFR0_EL1));
    value("ID_AA64MMFR0_EL1", READ_SYSREG(ID_AA64MMFR0_EL1));
    value("CNTFRQ_EL0", READ_SYSREG(CNTFRQ_EL0));
    value("CNTVCT_EL0", READ_SYSREG(CNTVCT_EL0));
    if (el == 8) value("SCTLR_EL2", READ_SYSREG(SCTLR_EL2));
    else value("SCTLR_EL1", READ_SYSREG(SCTLR_EL1));
    puts_uart("SHIM_DIAGNOSTIC_DONE\n");
    /* Leave the inherited watchdog untouched. A later reset is possible. */
    stop();
}
