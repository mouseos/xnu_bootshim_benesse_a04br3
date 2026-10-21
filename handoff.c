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
/* LK scanout geometry follows the working A04BR3 EDK2 DisplayInitDxe.
 * Publish it only for the explicit physical-display test and only when
 * FDT protects the complete four-buffer scanout buffer from managed RAM. */
int handoff_video_setup(struct xnu_video *video, const struct fdt_info *fdt,
                        const struct xnu_payload *p)
{
    *video = (struct xnu_video){0};
    const char flag[] = "a04br3.display_probe=1";
    int enabled = 0;
    for (unsigned i = 0; i + sizeof(flag) <= XNU_CMDLINE && p->command_line[i]; i++) {
        if (i && p->command_line[i-1] != ' ') continue;
        unsigned j = 0;
        while (flag[j] && flag[j] == p->command_line[i+j]) j++;
        if (!flag[j] && (!p->command_line[i+j] || p->command_line[i+j] == ' ')) enabled = 1;
    }
    if (!enabled) return 0;
    const uint64_t base = 0xbe9c0000, bytes = 800ULL * 1280 * 4 * 4;
    int reserved = 0;
    for (unsigned i = 0; i < fdt->reserved_count; i++) {
        if (inside(base, bytes, fdt->reserved[i].base, fdt->reserved[i].size)) reserved = 1;
    }
    if (!reserved) return 0;
    *video = (struct xnu_video){base, 1, 800 * 4, 800, 1280, 32};
    return 1;
}
static int overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    if (!n || !m) return 0;
    if (a + n < a || b + m < b) return 1;
    return a < b + m && b < a + n;
}
static int covered(uint64_t start, uint64_t size, const struct fdt_info *fdt)
{
    if (!size || start+size<start) return 0;
    uint64_t cursor=start,end=start+size;
    /* Adjacent FDT banks may form one contiguous managed interval. */
    for (unsigned pass=0;pass<fdt->memory_count;pass++) {
        uint64_t next=cursor;
        for (unsigned i=0;i<fdt->memory_count;i++) {
            uint64_t base=fdt->memory[i].base,limit=base+fdt->memory[i].size;
            if (limit<base) continue;
            if (base<=cursor && limit>next) next=limit;
        }
        if (next==cursor) return 0;
        if (next>=end) return 1;
        cursor=next;
    }
    return 0;
}
/* Version 3 stores only the final RAMDisk as bounded literal/zero runs.
 * Validate the complete stream before any destination write. */
int handoff_sparse(void *destination, const unsigned char *source, uint64_t length, uint64_t memory)
{
    unsigned char *out = destination;
    uint64_t at = 0, produced = 0;
    while (at < length) {
        if (length - at < 4) return -1;
        uint32_t tag = (uint32_t)source[at] | ((uint32_t)source[at+1] << 8) |
            ((uint32_t)source[at+2] << 16) | ((uint32_t)source[at+3] << 24);
        at += 4;
        uint32_t count = tag & 0x7fffffffU;
        if (!count || count > memory - produced) return -1;
        if (!(tag & 0x80000000U) && count > length - at) return -1;
        if (out) {
            for (uint32_t j = 0; j < count; j++)
                out[produced+j] = (tag & 0x80000000U) ? 0 : source[at+j];
        }
        produced += count;
        if (!(tag & 0x80000000U)) at += count;
    }
    return produced == memory ? 0 : -1;
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
    if ((p->version != 1 && p->version != 2 && p->version != 3 && p->version != 4 && p->version != 5 && p->version != 6) ||
        (p->version >= 2 && p->segment_count < 2) || (p->version == 5 && p->segment_count < 3) || (p->version == 6 && p->segment_count < 4) || !p->segment_count || p->segment_count > XNU_MAX_SEGMENTS ||
        p->total_size != length || p->segments_offset < sizeof(*p) ||
        (p->segments_offset & 7) || p->segments_offset > length ||
        p->segment_count * sizeof(struct xnu_segment) > length - p->segments_offset ||
        p->data_offset < p->segments_offset + p->segment_count * sizeof(struct xnu_segment) ||
        p->data_offset > length || (p->data_offset & 7) ||
        (p->virt_base & 0x1ffffff) || (p->phys_base & 0x1ffffff) ||
        !p->mem_size || p->mem_size > 0x60000000 || (p->mem_size & (XNU_PAGE - 1)) ||
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
    if (p->version >= 2) {
        const struct xnu_segment *rd = &segments[p->segment_count - 1];
        if ((rd->physical & (XNU_PAGE - 1)) ||
            (rd->memory_size & (XNU_PAGE - 1)) || !rd->file_size ||
            inside(plan->entry_pa, 4, rd->physical, rd->memory_size)) return -1;
    }
    if (p->version == 4) {
        /* v4: exactly one raw non-entry GPU asset after all kernel segments.
         * Its actual file length is separate from the 16KiB reservation. */
        const struct xnu_segment *fw = &segments[p->segment_count - 1];
        uint64_t kernel_end = p->phys_base;
        for (unsigned i = 0; i + 1 < p->segment_count; i++) {
            const struct xnu_segment *s = &segments[i];
            if (s->physical + s->memory_size > kernel_end)
                kernel_end = s->physical + s->memory_size;
            if (s->file_offset + s->file_size > fw->file_offset) return -1;
        }
        if (fw->physical != aligned(kernel_end) || fw->physical + fw->memory_size > 0x100000000ULL ||
            fw->file_size <= 4096 ||
            fw->file_size > 32UL * 1024 * 1024 || fw->memory_size != aligned(fw->file_size) ||
            fw->file_offset + ((fw->file_size + 7) & ~7ULL) != length) return -1;
        /* eMMC root only: no ambiguous rd= selector and no RAMDisk alias. */
        unsigned roots = 0;
        for (unsigned at = 0; at < XNU_CMDLINE && p->command_line[at];) {
            unsigned start = at;
            while (at < XNU_CMDLINE && p->command_line[at] && p->command_line[at] != ' ') at++;
            unsigned n = at - start;
            if (n >= 3 && p->command_line[start] == 'r' &&
                p->command_line[start+1] == 'd' && p->command_line[start+2] == '=') {
                static const char root[] = "rd=emmcroot0";
                if (n != sizeof(root)-1) return -1;
                for (unsigned j = 0; j < n; j++) if (p->command_line[start+j] != root[j]) return -1;
                roots++;
            }
            if (at < XNU_CMDLINE && p->command_line[at] == ' ') at++;
        }
        if (roots != 1) return -1;
    }
    if (p->version == 5 || p->version == 6) {
        /* Two or three raw assets form both the physical and source-file tail. */
        const struct xnu_segment *wmt = &segments[p->segment_count - 2];
        const struct xnu_segment *gpu = &segments[p->segment_count - 1];
        unsigned assets = p->version == 6 ? 3 : 2;
        uint64_t kernel_end = p->phys_base, source_end = p->data_offset;
        for (unsigned i = 0; i + assets < p->segment_count; i++) {
            const struct xnu_segment *s = &segments[i];
            if (s->physical + s->memory_size > kernel_end) kernel_end = s->physical + s->memory_size;
            if (s->file_offset != ((source_end+7)&~7ULL)) return -1;
            source_end = s->file_offset + s->file_size;
        }
        if (p->version == 6) {
            const struct xnu_segment *wlan = &segments[p->segment_count - 3];
            if (wlan->physical != aligned(kernel_end) ||
                (wlan->physical & (XNU_PAGE-1)) || wlan->file_size < 32 ||
                wlan->file_size > 16UL*1024*1024 ||
                wlan->memory_size != aligned(wlan->file_size) ||
                wlan->file_offset != ((source_end+7)&~7ULL) ||
                inside(plan->entry_pa,4,wlan->physical,wlan->memory_size)) return -1;
            kernel_end = wlan->physical + wlan->memory_size;
            source_end = wlan->file_offset + wlan->file_size;
        }
        if (wmt->physical != aligned(kernel_end) || (wmt->physical & (XNU_PAGE-1)) ||
            wmt->file_size < 32 || wmt->file_size > 32UL*1024*1024 ||
            wmt->memory_size != aligned(wmt->file_size) ||
            gpu->physical != wmt->physical + wmt->memory_size ||
            gpu->file_size <= 4096 || gpu->file_size > 32UL*1024*1024 ||
            gpu->memory_size != aligned(gpu->file_size) ||
            gpu->physical + gpu->memory_size > 0x100000000ULL ||
            wmt->file_offset != ((source_end+7)&~7ULL) ||
            gpu->file_offset != wmt->file_offset + ((wmt->file_size+7)&~7ULL) ||
            gpu->file_offset + ((gpu->file_size+7)&~7ULL) != length ||
            inside(plan->entry_pa,4,wmt->physical,wmt->memory_size) ||
            inside(plan->entry_pa,4,gpu->physical,gpu->memory_size)) return -1;
        unsigned roots = 0;
        for (unsigned at = 0; at < XNU_CMDLINE && p->command_line[at];) {
            unsigned start = at;
            while (at < XNU_CMDLINE && p->command_line[at] && p->command_line[at] != ' ') {
                char c = p->command_line[at];
                if (c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') return -1;
                at++;
            }
            unsigned n = at-start;
            if (n >= 3 && p->command_line[start]=='r' && p->command_line[start+1]=='d' && p->command_line[start+2]=='=') {
                static const char root[] = "rd=emmcroot0";
                if (n != sizeof(root)-1) return -1;
                for (unsigned j=0;j<n;j++) if (p->command_line[start+j]!=root[j]) return -1;
                roots++;
            }
            if (at<XNU_CMDLINE && p->command_line[at]==' ') at++;
        }
        if (roots != 1) return -1;
    }
    if (p->version == 3) {
        const struct xnu_segment *rd = &segments[p->segment_count - 1];
        if (handoff_sparse(0, (const unsigned char *)p + rd->file_offset,
                           rd->file_size, rd->memory_size)) return -1;
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
        if (p->version == 3 && i + 1 == p->segment_count) {
            if (handoff_sparse((void *)(uintptr_t)s->physical,
                    (const unsigned char *)p + s->file_offset, s->file_size, s->memory_size))
                return "XNU_RAMDISK_DECODE_FAILED";
            continue;
        }
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
    if (handoff_video_setup(&ba.video, fdt, p)) {
        /* Small RGB bands on all four LK buffers identify this test without
         * reprogramming clocks, panel, DSI or overlay registers. */
        for (unsigned page = 0; page < 4; page++) {
            volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)
                (ba.video.base + page * 800UL * 1280 * 4);
            for (unsigned y = 0; y < 24; y++) {
                for (unsigned x = 0; x < 800; x++) {
                    pixels[y * 800 + x] = x < 267 ? 0xffff0000 :
                        x < 534 ? 0xff00ff00 : 0xff0000ff;
                }
            }
        }
        __asm__ volatile("dsb sy" ::: "memory");
        handoff_trace("XNU_LK_FRAMEBUFFER", ba.video.base);
    }

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
