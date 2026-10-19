/* SPDX-License-Identifier: MIT */
#ifndef A04BR3_FDT_H
#define A04BR3_FDT_H
#include <stddef.h>
#include <stdint.h>
#define FDT_MAX_MEMORY 8
#define FDT_MAX_RESERVED 64
struct fdt_range { uint64_t base, size; };
struct fdt_info {
    uint32_t bytes;
    unsigned memory_count, reserved_count;
    struct fdt_range memory[FDT_MAX_MEMORY], reserved[FDT_MAX_RESERVED];
    uint64_t initrd_start, initrd_end;
};
/* Read-only, bounded inspection. Does not allocate or authorize a memory map. */
int fdt_inspect(const void *data, size_t available, struct fdt_info *info);
#endif
