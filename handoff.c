/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 */
#include "handoff.h"
static uint64_t aligned(uint64_t value)
{
    return (value + XNU_PAGE - 1) & ~(XNU_PAGE - 1);
}
static int inside(uint64_t start, uint64_t size, uint64_t base, uint64_t span)
{
    return start >= base && start - base <= span && size <= span - (start - base);
}
static int overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    if (!n || !m) return 0;
    if (a + n < a || b + m < b) return 1;
    return a < b + m && b < a + n;
}
static int covered(uint64_t start, uint64_t size, const struct fdt_info *fdt)
{
    for (unsigned i = 0; i < fdt->memory_count; i++)
        if (inside(start, size, fdt->memory[i].base, fdt->memory[i].size)) return 1;
    return 0;
}
int handoff_validate(const void *payload, size_t length, uint64_t payload_pa,
                     const struct fdt_info *fdt, uint64_t fdt_pa,
                     uint64_t shim_start, uint64_t shim_end, struct handoff_plan *plan)
{
    if (length < sizeof(struct xnu_payload) || length > 0x8000000 ||
        ((uintptr_t)payload & 7) || payload_pa + length < payload_pa ||
        fdt->memory_count > FDT_MAX_MEMORY || fdt->reserved_count > FDT_MAX_RESERVED ||
        shim_end < shim_start) return -1;
    const struct xnu_payload *p = payload;
    static const char magic[] = "A04XNU01";
    for (unsigned i = 0; i < 8; i++) if (p->magic[i] != magic[i]) return -1;
    if (p->version != 1 || !p->segment_count || p->segment_count > XNU_MAX_SEGMENTS ||
        p->total_size != length || p->segments_offset < sizeof(*p) ||
        (p->segments_offset & 7) || p->segments_offset > length ||
        p->segment_count * sizeof(struct xnu_segment) > length - p->segments_offset ||
        p->data_offset < p->segments_offset + p->segment_count * sizeof(struct xnu_segment) ||
        p->data_offset > length || (p->data_offset & 7) ||
        (p->virt_base & 0x1ffffff) || (p->phys_base & 0x1ffffff) ||
        !p->mem_size || p->mem_size > 0x40000000 || (p->mem_size & (XNU_PAGE - 1)) ||
        p->virt_base < 0xffffff8000000000ULL || p->virt_base + p->mem_size < p->virt_base ||
        !covered(p->phys_base, p->mem_size, fdt) ||
        !covered(payload_pa, length, fdt) ||
        overlap(p->phys_base, p->mem_size, payload_pa, length) ||
        overlap(p->phys_base, p->mem_size, fdt_pa, fdt->bytes) ||
        overlap(p->phys_base, p->mem_size, shim_start, shim_end - shim_start)) return -1;
    for (unsigned i = 0; i < fdt->reserved_count; i++)
        if (overlap(p->phys_base, p->mem_size, fdt->reserved[i].base, fdt->reserved[i].size)) return -1;
    int terminated = 0;
    for (unsigned i = 0; i < XNU_CMDLINE; i++) if (!p->command_line[i]) { terminated = 1; break; }
    if (!terminated || p->entry_va < p->virt_base || p->entry_va - p->virt_base >= p->mem_size) return -1;
    plan->entry_pa = p->phys_base + (p->entry_va - p->virt_base);
    const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
    uint64_t end = p->phys_base;
    unsigned entry_found = 0;
    for (unsigned i = 0; i < p->segment_count; i++) {
        const struct xnu_segment *s = &segments[i];
        if (!s->memory_size || (s->physical & 7) || (s->file_offset & 7) ||
            !inside(s->physical, s->memory_size, p->phys_base, p->mem_size) ||
            s->file_size > s->memory_size || s->file_offset < p->data_offset ||
            s->file_offset > length || s->file_size > length - s->file_offset) return -1;
        for (unsigned j = 0; j < i; j++)
            if (overlap(s->physical, s->memory_size, segments[j].physical, segments[j].memory_size)) return -1;
        if (inside(plan->entry_pa, 4, s->physical, s->file_size)) entry_found++;
        if (s->physical + s->memory_size > end) end = s->physical + s->memory_size;
    }
    if (entry_found != 1 || aligned(end) != p->kernel_end_pa) return -1;
    plan->adt_pa = aligned(end);
    /* Reserve a full page for ADT and a full page for boot_args. */
    plan->args_pa = plan->adt_pa + XNU_PAGE;
    plan->top = plan->args_pa + XNU_PAGE;
    if (!inside(plan->adt_pa, 2 * XNU_PAGE, p->phys_base, p->mem_size)) return -1;
    return 0;
}

#ifndef HANDOFF_HOST_TEST
extern void handoff_trace(const char *, uint64_t);
extern void handoff_message(const char *);
extern char _head[], _end[];
static unsigned char adt_buffer[XNU_PAGE] __attribute__((aligned(8)));
static void copy_bytes(uint64_t dest, const void *source, uint64_t size)
{
    volatile unsigned char *d = (void *)(uintptr_t)dest;
    const unsigned char *s = source;
    for (uint64_t i = 0; i < size; i++) d[i] = s[i];
}
static void zero_bytes(uint64_t dest, uint64_t size)
{
    volatile unsigned char *d = (void *)(uintptr_t)dest;
    for (uint64_t i = 0; i < size; i++) d[i] = 0;
}
const char *handoff_boot(const struct fdt_info *fdt, uint64_t fdt_pa)
{
    if (fdt->initrd_end <= fdt->initrd_start || fdt->initrd_start & 7)
        return "XNU_PAYLOAD_NOT_PRESENT";
    uint64_t size = fdt->initrd_end - fdt->initrd_start;
    if (size < sizeof(struct xnu_payload) || size > 0x8000000 ||
        !covered(fdt->initrd_start, size, fdt)) return "XNU_PAYLOAD_NOT_PRESENT";
    const struct xnu_payload *p = (void *)(uintptr_t)fdt->initrd_start;
    struct handoff_plan plan;
    if (handoff_validate(p, size, fdt->initrd_start, fdt, fdt_pa,
                         (uintptr_t)_head, (uintptr_t)_end, &plan)) return "XNU_PAYLOAD_PLAN_REJECTED";
    uint64_t el, sctlr, timebase;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timebase));
    if (el != 4 || (sctlr & 0x1005) || timebase > 0xffffffff) return "XNU_HANDOFF_CPU_STATE_REJECTED";
    unsigned char seed[64];
    handoff_message("XNU_TRNG_BEGIN\n");
    int rng_status = mt8167_random_seed(seed, (uint32_t)timebase);
    if (rng_status) {
        handoff_trace("XNU_TRNG_ERROR", (uint64_t)(int64_t)rng_status);
        return "XNU_BOOT_ENTROPY_UNAVAILABLE";
    }
    handoff_message("XNU_TRNG_READY\n");
    size_t adt_size = xnu_make_adt_seeded(adt_buffer, sizeof(adt_buffer), p,
                                        (uint32_t)timebase, seed, sizeof(seed));
    zero_bytes((uintptr_t)seed, sizeof(seed));
    if (!adt_size) return "XNU_ADT_BUILD_FAILED";
    handoff_message("XNU_BOOT_ARGS ");
    handoff_message(p->command_line);
    handoff_message("\n");
    handoff_trace("XNU_physBase", p->phys_base);
    handoff_trace("XNU_memSize", p->mem_size);
    handoff_trace("XNU_entryPA", plan.entry_pa);
    handoff_trace("XNU_ADT_PA", plan.adt_pa);
    handoff_trace("XNU_boot_args_PA", plan.args_pa);
    handoff_trace("XNU_topOfKernelData", plan.top);
    const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
    for (unsigned i = 0; i < p->segment_count; i++) {
        const struct xnu_segment *s = &segments[i];
        copy_bytes(s->physical, (const unsigned char *)p + s->file_offset, s->file_size);
        zero_bytes(s->physical + s->file_size, s->memory_size - s->file_size);
    }
    zero_bytes(plan.adt_pa, 2 * XNU_PAGE);
    copy_bytes(plan.adt_pa, adt_buffer, adt_size);
    zero_bytes((uintptr_t)adt_buffer, sizeof(adt_buffer));
    struct xnu_boot_args ba;
    zero_bytes((uintptr_t)&ba, sizeof(ba));
    ba.revision = ba.version = 2;
    ba.virt_base = p->virt_base; ba.phys_base = p->phys_base;
    ba.mem_size = ba.mem_size_actual = p->mem_size;
    ba.top_of_kernel_data = plan.top;
    ba.device_tree = p->virt_base + (plan.adt_pa - p->phys_base);
    ba.device_tree_length = (uint32_t)adt_size;
    for (unsigned i = 0; i < XNU_CMDLINE; i++) ba.command_line[i] = p->command_line[i];
    copy_bytes(plan.args_pa, &ba, sizeof(ba));
    handoff_message("XNU_HANDOFF_JUMP\n");
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    ((void (*)(uint64_t))(uintptr_t)plan.entry_pa)(plan.args_pa);
    return "XNU_ENTRY_RETURNED";
}
#endif
