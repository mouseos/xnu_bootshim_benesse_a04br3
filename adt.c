/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 * Apple flattened device-tree serialization; no iPhone identity or seed data.
 */
#include "handoff.h"
struct writer { unsigned char *data; size_t used, capacity; int error; };
static void byte(struct writer *w, unsigned char value)
{
    if (w->used >= w->capacity) w->error = 1;
    else w->data[w->used] = value;
    w->used++;
}
static void u32(struct writer *w, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) byte(w, (unsigned char)(value >> (8 * i)));
}
static void node(struct writer *w, uint32_t properties, uint32_t children)
{
    u32(w, properties); u32(w, children);
}
static void prop(struct writer *w, const char *key, const void *value, uint32_t length)
{
    unsigned i = 0;
    while (i < 32 && key[i]) { byte(w, (unsigned char)key[i]); i++; }
    if (i == 32) { w->error = 1; return; }
    while (i++ < 32) byte(w, 0);
    u32(w, length);
    const unsigned char *p = value;
    for (uint32_t j = 0; j < length; j++) byte(w, p[j]);
    while (w->used & 3) byte(w, 0);
}
static void text(struct writer *w, const char *key, const char *value)
{
    uint32_t length = 0;
    while (value[length]) length++;
    prop(w, key, value, length + 1);
}
static void word(struct writer *w, const char *key, uint32_t value)
{
    prop(w, key, &value, sizeof(value));
}
/* Full donor copies do not expose native BSD rootless flags via Linux stat. */
static int command_flag(const struct xnu_payload *p, const char *flag)
{
    unsigned length=0;
    while (flag[length]) length++;
    for (unsigned i=0;i+length<sizeof(p->command_line);i++) {
        if (!p->command_line[i]) break;
        if (i && p->command_line[i-1]!=' ') continue;
        unsigned j=0;
        while (flag[j] && flag[j]==p->command_line[i+j]) j++;
        if (!flag[j] && (!p->command_line[i+j] || p->command_line[i+j]==' ')) return 1;
    }
    return 0;
}
size_t xnu_make_adt_seeded(void *buffer, size_t capacity, const struct xnu_payload *p,
                           uint32_t timebase, const unsigned char *seed, size_t seed_bytes)
{
    if ((seed_bytes && (!seed || seed_bytes != 64)) || (!seed_bytes && seed)) return 0;
    struct writer w = { buffer, 0, capacity, 0 };
    uint64_t range[2] = {p->phys_base, p->kernel_end_pa - p->phys_base};
    node(&w, 6, 4);
    text(&w, "name", "device-tree");
    text(&w, "device_type", "device-tree");
    static const char compatible[] = "benesse,a04br3\0mediatek,mt8167";
    prop(&w, "compatible", compatible, sizeof(compatible));
    text(&w, "model", "Benesse TAB-A04-BR3");
    word(&w, "#address-cells", 2); word(&w, "#size-cells", 2);
    int full_root=command_flag(p, "a04br3.full_root=1");
    int display=command_flag(p, "a04br3.display_probe=1");
    node(&w, 5 + (seed_bytes != 0) + 2 * display + 3 * full_root, 1 + full_root);
    text(&w, "name", "chosen");
    /* SHA-384("A04BR3 development boot namespace v1"). This names the
     * unsealed development preboot namespace; it is not Apple secure boot.
     * iOS mount needs this property even when no firmware bind is required. */
    static const unsigned char development_manifest[48] = {0x06, 0xc2, 0x72, 0x0d, 0x73, 0x22, 0x16, 0x6d, 0x2d, 0x70, 0xfd, 0x97, 0xed, 0x5a, 0x8c, 0x56, 0x9a, 0xf4, 0x11, 0x58, 0x93, 0xe2, 0x03, 0x2d, 0xd8, 0x61, 0x2e, 0xb7, 0x6d, 0x6e, 0x79, 0xfe, 0x4a, 0x63, 0x83, 0xc1, 0x11, 0xdb, 0x2a, 0x61, 0xe1, 0xb2, 0x35, 0x8c, 0x2e, 0x94, 0x21, 0x93};
    prop(&w, "boot-manifest-hash", development_manifest, sizeof(development_manifest));
    if (full_root) {
        /* Stable synthetic development namespace, NOT a physical ECID or an
         * Apple identity. Required by native gethostuuid/ByHost preferences. */
        uint64_t development_chip = 0x4130344252330001ULL;
        prop(&w, "unique-chip-id", &development_chip, sizeof(development_chip));
        word(&w, "chip-id", 0x8167);
        text(&w, "crypto-hash-method", "sha2-384");
    }
    if (seed_bytes) prop(&w, "random-seed", seed, (uint32_t)seed_bytes);
    prop(&w, "dram-base", &p->phys_base, 8);
    prop(&w, "dram-size", &p->mem_size, 8);
    word(&w, "debug-enabled", 1);
    if (display) {
        /* Explicit 1:1 logical pixels; absent scale defaults to zero in
         * MobileGestalt and yields an invalid FBSDisplayMode. ios-sweet notes. */
        word(&w, "display-scale", 1);
        word(&w, "display-rotation", 0);
    }
    node(&w, 2 + (p->version >= 2) + (p->version >= 5) + (p->version == 6), 0);
    text(&w, "name", "memory-map"); prop(&w, "Kernel-A04BR3", range, sizeof(range));
    /* v2's final validated load segment is the root memory device. */
    if (p->version == 2 || p->version == 3) {
        const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
        const struct xnu_segment *rd = &segments[p->segment_count - 1];
        uint64_t ramdisk[2] = {rd->physical, rd->memory_size};
        prop(&w, "RAMDisk", ramdisk, sizeof(ramdisk));
    }
    if (p->version == 4 || p->version == 5 || p->version == 6) {
        const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
        const struct xnu_segment *fw = &segments[p->segment_count - 1];
        uint64_t firmware[2] = {fw->physical, fw->file_size};
        prop(&w, "GPUFirmware-A04BR3", firmware, sizeof(firmware));
    }
    if (p->version == 5 || p->version == 6) {
        const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
        const struct xnu_segment *fw = &segments[p->segment_count - 2];
        uint64_t firmware[2] = {fw->physical, fw->file_size};
        prop(&w, "WMTFirmware-A04BR3", firmware, sizeof(firmware));
    }
    if (p->version == 6) {
        const struct xnu_segment *segments = (const void *)((const unsigned char *)p + p->segments_offset);
        const struct xnu_segment *fw = &segments[p->segment_count - 3];
        uint64_t firmware[2] = {fw->physical, fw->file_size};
        prop(&w, "WLANFirmware-A04BR3", firmware, sizeof(firmware));
    }
    if (full_root) {
        node(&w, 3, 0);text(&w, "name", "asmb");
        uint64_t filesystem_development=2; /* CSR_ALLOW_UNRESTRICTED_FS only. */
        prop(&w, "lp-sip0", &filesystem_development, 8);
        word(&w, "lp-sip1", 1); /* explicit unauthenticated development root */
    }
    /* Physical Aff0 0..3, one homogeneous MT8167 A35 cluster.
     * Expose only the explicitly requested development topology. */
    unsigned cpus = command_flag(p, "cpus=4") ? 4 : command_flag(p, "cpus=2") ? 2 : 1;
    node(&w, 3, cpus);
    text(&w, "name", "cpus"); word(&w, "#address-cells", 1); word(&w, "#size-cells", 0);
    static const char compatible_cpu[] = "arm,cortex-a35\0ARM,v8";
    for (unsigned i = 0; i < cpus; i++) {
        char name[] = "cpu0"; name[3] += i;
        node(&w, 8, 0);
        text(&w, "name", name); text(&w, "device_type", "cpu");
        prop(&w, "compatible", compatible_cpu, sizeof(compatible_cpu)); word(&w, "reg", i);
        text(&w, "state", i ? "stopped" : "running"); word(&w, "timebase-frequency", timebase);
        word(&w, "cpu-id", i); word(&w, "cluster-id", 0);
    }
    node(&w, 6, 2 + display);
    text(&w, "name", "arm-io"); text(&w, "device_type", "soc");
    text(&w, "compatible", "mediatek,mt8167");
    uint64_t io_ranges[3] = {0, 0x10000000, 0x10000000};
    prop(&w, "ranges", io_ranges, sizeof(io_ranges));
    word(&w, "#address-cells", 2); word(&w, "#size-cells", 2);
    node(&w, 4, 0);
    text(&w, "name", "uart0"); text(&w, "device_type", "serial");
    text(&w, "compatible", "mediatek,mt6577-uart");
    uint64_t uart[2] = {0x01005000, 0x1000}; prop(&w, "reg", uart, sizeof(uart));
    node(&w, 5, 0);
    text(&w, "name", "interrupt-controller");
    text(&w, "compatible", "arm,gic-400");
    text(&w, "interrupt-controller", "master");
    uint64_t gic[4] = {0x00310000, 0x1000, 0x0032f000, 0x2000};
    prop(&w, "reg", gic, sizeof(gic));
    word(&w, "virtual-timer-intid", 27);
    /* Native IOMobileFramebuffer enumerates display-subsystem children in
     * IODeviceTree:/arm-io before looking up framebuffer services. See
     * ios-sweet 0046. Publishing an IOService alone is insufficient. */
    if (display) {
        node(&w, 4, 0);
        text(&w, "name", "disp0");
        text(&w, "device_type", "display-subsystem");
        word(&w, "canvas-width", 800);
        word(&w, "canvas-height", 1280);
    }
    /* pmap_compute_max_asids requires /defaults. An absent override selects
     * XNU's MAX_ASIDS, so do not invent a board-specific ASID count. */
    node(&w, 2, 0);
    text(&w, "name", "defaults");
    /* This board has no Apple effaceable-storage hardware. */
    word(&w, "no-effaceable-storage", 1);
    return w.error ? 0 : w.used;
}

size_t xnu_make_adt(void *buffer, size_t capacity, const struct xnu_payload *p,
                    uint32_t timebase)
{
    return xnu_make_adt_seeded(buffer, capacity, p, timebase, 0, 0);
}
