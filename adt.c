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
size_t xnu_make_adt(void *buffer, size_t capacity, const struct xnu_payload *p,
                    uint32_t timebase)
{
    struct writer w = { buffer, 0, capacity, 0 };
    uint64_t range[2] = {p->phys_base, p->kernel_end_pa - p->phys_base};
    node(&w, 6, 3);
    text(&w, "name", "device-tree");
    text(&w, "device_type", "device-tree");
    static const char compatible[] = "benesse,a04br3\0mediatek,mt8167";
    prop(&w, "compatible", compatible, sizeof(compatible));
    text(&w, "model", "Benesse TAB-A04-BR3");
    word(&w, "#address-cells", 2); word(&w, "#size-cells", 2);
    node(&w, 4, 1);
    text(&w, "name", "chosen");
    prop(&w, "dram-base", &p->phys_base, 8);
    prop(&w, "dram-size", &p->mem_size, 8);
    word(&w, "debug-enabled", 1);
    node(&w, 2, 0);
    text(&w, "name", "memory-map"); prop(&w, "Kernel-A04BR3", range, sizeof(range));
    node(&w, 3, 1);
    text(&w, "name", "cpus"); word(&w, "#address-cells", 1); word(&w, "#size-cells", 0);
    node(&w, 8, 0);
    text(&w, "name", "cpu0"); text(&w, "device_type", "cpu");
    static const char cpu[] = "arm,cortex-a35\0ARM,v8";
    prop(&w, "compatible", cpu, sizeof(cpu)); word(&w, "reg", 0);
    text(&w, "state", "running"); word(&w, "timebase-frequency", timebase);
    word(&w, "cpu-id", 0); word(&w, "cluster-id", 0);
    node(&w, 5, 1);
    text(&w, "name", "arm-io"); text(&w, "device_type", "soc");
    text(&w, "compatible", "mediatek,mt8167");
    word(&w, "#address-cells", 2); word(&w, "#size-cells", 2);
    node(&w, 4, 0);
    text(&w, "name", "uart0"); text(&w, "device_type", "serial");
    text(&w, "compatible", "mediatek,mt6577-uart");
    uint64_t uart[2] = {0x11005000, 0x1000}; prop(&w, "reg", uart, sizeof(uart));
    return w.error ? 0 : w.used;
}
