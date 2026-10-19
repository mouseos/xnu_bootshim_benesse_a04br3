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
