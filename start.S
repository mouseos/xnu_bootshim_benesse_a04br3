/* SPDX-License-Identifier: MIT
 * AArch64 Image framing follows dipa-studio/ios-sweet (see CREDITS.md).
 * This entry supports the observed MT8167 EL2 handoff and EL1 diagnostics.
 */
.section .text.head, "ax"
.global _head
_head:
    b _start
    .long 0
    .quad 0
    .quad _end
    .quad 0xa
    .quad 0, 0, 0
    .ascii "ARM\x64"
    .long 0
.text
.global _start
_start:
    msr daifset, #0xf
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3
    mov x23, sp
    msr spsel, #1
    adr x1, _stack_top
    mov sp, x1
    adr x1, vectors
    mrs x2, CurrentEL
    cmp x2, #8
    b.eq 1f
    cmp x2, #4
    b.ne halt
    msr vbar_el1, x1
    b 2f
1:  msr vbar_el2, x1
2:  isb
    adr x1, _bss_start
    adr x2, _bss_end
3:  cmp x1, x2
    b.hs 4f
    str xzr, [x1], #8
    b 3b
4:  mov x0, x19
    mov x1, x20
    mov x2, x21
    mov x3, x22
    mov x4, x23
    bl shim_main
halt:
    wfe
    b halt

/* Enter the EL1h state required by XNU start_first_cpu; x0 retains the FDT.
 * Called only with EL2 MMU and caches off, as checked by shim_main.
 */
.global enter_el1
enter_el1:
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #0x33ff
    msr cptr_el2, x1
    msr hstr_el2, xzr
    msr vttbr_el2, xzr
    mov x1, #0x0800
    movk x1, #0x30d0, lsl #16
    msr sctlr_el1, x1
    adr x1, vectors
    msr vbar_el1, x1
    adr x1, _stack_top
    msr sp_el1, x1
    adr x1, el1_start
    msr elr_el2, x1
    mov x1, #0x3c5
    msr spsr_el2, x1
    dsb sy
    isb
    eret
el1_start:
    mov x29, xzr
    mov x30, xzr
    bl shim_el1
    b halt

.macro vector slot
    .balign 128
    mov x0, #\slot
    b exception_report
.endm
.balign 2048
vectors:
    vector 0
    vector 1
    vector 2
    vector 3
    vector 4
    vector 5
    vector 6
    vector 7
    vector 8
    vector 9
    vector 10
    vector 11
    vector 12
    vector 13
    vector 14
    vector 15
