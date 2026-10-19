/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 yuma shimizu
 */
#include "fdt.h"

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}
static int equal(const unsigned char *p, size_t n, const char *s)
{
    size_t i = 0;
    while (i < n && s[i] && p[i] == (unsigned char)s[i]) i++;
    return i == n && !s[i];
}
static uint64_t cells(const unsigned char *p, unsigned count)
{
    return count == 2 ? be64(p) : be32(p);
}
static int append(struct fdt_range *ranges, unsigned *count, unsigned max,
                  uint64_t base, uint64_t size)
{
    if (!size) return 0;
    if (*count == max || base + size < base) return -1;
    ranges[*count].base = base;
    ranges[*count].size = size;
    (*count)++;
    return 0;
}
struct frame {
    unsigned address_cells, size_cells, child_address_cells, child_size_cells;
    unsigned memory, reserved_root, reserved_child, chosen;
};

int fdt_inspect(const void *data, size_t available, struct fdt_info *info)
{
    const unsigned char *b = data;
    struct frame frames[16];
    unsigned depth = 0, root_seen = 0;
    if (available < 40 || be32(b) != 0xd00dfeed) return -1;
    uint32_t total = be32(b + 4), off = be32(b + 8), strings = be32(b + 12);
    uint32_t reserve = be32(b + 16), ssize = be32(b + 32), tsize = be32(b + 36);
    if (total > available || total > 0x100000 || total < 40 || be32(b + 20) != 17 ||
        off < 40 || (off & 3) || off > total || tsize > total - off ||
        strings > total || ssize > total - strings || reserve < 40 || (reserve & 7)) return -1;
    info->bytes = total;
    info->memory_count = info->reserved_count = 0;
    info->initrd_start = info->initrd_end = 0;
    for (;;) {
        if (reserve > total || total - reserve < 16) return -1;
        uint64_t base = be64(b + reserve), size = be64(b + reserve + 8);
        reserve += 16;
        if (!base && !size) break;
        if (append(info->reserved, &info->reserved_count, FDT_MAX_RESERVED, base, size)) return -1;
    }
    size_t end = off + tsize, p = off;
    while (p <= end && end - p >= 4) {
        unsigned token = be32(b + p);
        p += 4;
        if (token == 1) { /* BEGIN_NODE */
            if (depth == 16 || (!depth && root_seen)) return -1;
            size_t name = p;
            while (p < end && b[p]) p++;
            if (p == end) return -1;
            size_t length = p - name;
            struct frame *f = &frames[depth];
            f->address_cells = depth ? frames[depth - 1].child_address_cells : 2;
            f->size_cells = depth ? frames[depth - 1].child_size_cells : 1;
            f->child_address_cells = 2;
            f->child_size_cells = 1;
            size_t kind_length = 0;
            while (kind_length < length && b[name + kind_length] != '@') kind_length++;
            f->memory = depth == 1 && equal(b + name, kind_length, "memory");
            f->reserved_root = depth == 1 && equal(b + name, length, "reserved-memory");
            f->reserved_child = depth == 2 && frames[1].reserved_root;
            f->chosen = depth == 1 && equal(b + name, length, "chosen");
            if (!depth) { if (length) return -1; root_seen = 1; }
            depth++;
            p = (p + 4) & ~(size_t)3;
        } else if (token == 2) { /* END_NODE */
            if (!depth) return -1;
            depth--;
        } else if (token == 3) { /* PROP */
            if (!depth || end - p < 8) return -1;
            uint32_t length = be32(b + p), name = be32(b + p + 4);
            p += 8;
            if (length > end - p || name >= ssize) return -1;
            size_t n = 0;
            while (n < ssize - name && b[strings + name + n]) n++;
            if (n == ssize - name) return -1;
            const unsigned char *key = b + strings + name;
            struct frame *f = &frames[depth - 1];
            if (equal(key, n, "#address-cells")) {
                if (length != 4 || be32(b + p) > 2) return -1;
                f->child_address_cells = be32(b + p);
            } else if (equal(key, n, "#size-cells")) {
                if (length != 4 || be32(b + p) > 2) return -1;
                f->child_size_cells = be32(b + p);
            } else if ((f->memory || f->reserved_child) && equal(key, n, "reg")) {
                unsigned ac = f->address_cells, sc = f->size_cells;
                if (!ac || !sc || length % (4 * (ac + sc))) return -1;
                for (size_t at = 0; at < length; at += 4 * (ac + sc)) {
                    uint64_t base = cells(b + p + at, ac);
                    uint64_t size = cells(b + p + at + 4 * ac, sc);
                    if (f->memory) {
                        if (append(info->memory, &info->memory_count, FDT_MAX_MEMORY, base, size)) return -1;
                    } else if (append(info->reserved, &info->reserved_count, FDT_MAX_RESERVED, base, size)) return -1;
                }
            } else if (f->chosen && equal(key, n, "linux,initrd-start")) {
                if (length != 4 && length != 8) return -1;
                info->initrd_start = cells(b + p, length / 4);
            } else if (f->chosen && equal(key, n, "linux,initrd-end")) {
                if (length != 4 && length != 8) return -1;
                info->initrd_end = cells(b + p, length / 4);
            }
            p = (p + length + 3) & ~(size_t)3;
        } else if (token == 4) { /* NOP */
            continue;
        } else if (token == 9) { /* END */
            return root_seen && !depth && info->memory_count ? 0 : -1;
        } else return -1;
    }
    return -1;
}
