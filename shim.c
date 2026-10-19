/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * Diagnostic stage only: no kernel launch, storage access or power changes.
 */
#include <stdint.h>
#include "fdt.h"

__attribute__((noreturn)) void enter_el1(uint64_t dtb);
static struct fdt_info observed_fdt;

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
    if (el == 8) {
        /* Do not discard live mappings or dirty cache data inherited from LK. */
        if (READ_SYSREG(SCTLR_EL2) & ((1u << 0) | (1u << 2) | (1u << 12))) {
            puts_uart("EL1_TRANSITION_REFUSED_MMU_OR_CACHE_ON\n");
            stop();
        }
        value("HCR_EL2_before", READ_SYSREG(HCR_EL2));
        value("CNTHCTL_EL2_before", READ_SYSREG(CNTHCTL_EL2));
        puts_uart("ENTER_EL1\n");
        enter_el1(x0);
    }
    if (el == 4) {
        extern void shim_el1(uint64_t dtb);
        shim_el1(x0);
    }
    /* Leave the inherited watchdog untouched. A later reset is possible. */
    stop();
}

__attribute__((noreturn)) void shim_el1(uint64_t dtb)
{
    puts_uart("XNU_A04BR3_EL1_DIAGNOSTIC_V2\n");
    value("CurrentEL", READ_SYSREG(CurrentEL));
    if (READ_SYSREG(CurrentEL) != 4) {
        puts_uart("EL1_DIAGNOSTIC_WRONG_EXCEPTION_LEVEL\n");
        stop();
    }
    value("SCTLR_EL1", READ_SYSREG(SCTLR_EL1));
    value("CNTFRQ_EL0", READ_SYSREG(CNTFRQ_EL0));
    value("CNTVCT_EL0", READ_SYSREG(CNTVCT_EL0));
    value("DTB", dtb);
    /* Restrict all speculative input parsing to the known low DRAM window. */
    if (dtb < 0x40000000 || dtb > 0x7ff00000 || (dtb & 7) ||
        fdt_inspect((const void *)(uintptr_t)dtb, 0x100000, &observed_fdt)) {
        puts_uart("FDT_INVALID\n");
        stop();
    }
    value("FDT_bytes", observed_fdt.bytes);
    value("FDT_memory_count", observed_fdt.memory_count);
    for (unsigned i = 0; i < observed_fdt.memory_count; i++) {
        value("FDT_memory_base", observed_fdt.memory[i].base);
        value("FDT_memory_size", observed_fdt.memory[i].size);
    }
    value("FDT_reserved_count", observed_fdt.reserved_count);
    for (unsigned i = 0; i < observed_fdt.reserved_count; i++) {
        value("FDT_reserved_base", observed_fdt.reserved[i].base);
        value("FDT_reserved_size", observed_fdt.reserved[i].size);
    }
    value("FDT_initrd_start", observed_fdt.initrd_start);
    value("FDT_initrd_end", observed_fdt.initrd_end);
    puts_uart("SHIM_EL1_DIAGNOSTIC_DONE\n");
    stop();
}
