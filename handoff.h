/* SPDX-License-Identifier: MIT
 * Interfaces follow Apple xnu-8796.141.3 pexpert/arm64/boot.h.
 */
#ifndef A04BR3_HANDOFF_H
#define A04BR3_HANDOFF_H
#include <stddef.h>
#include <stdint.h>
#include "fdt.h"
#define XNU_PAGE 0x4000UL
#define XNU_CMDLINE 608
#define XNU_MAX_SEGMENTS 16
struct xnu_video { uint64_t base, display, rowbytes, width, height, depth; };
struct xnu_boot_args {
    uint16_t revision, version;
    uint64_t virt_base, phys_base, mem_size, top_of_kernel_data;
    struct xnu_video video;
    uint32_t machine_type;
    uint64_t device_tree;
    uint32_t device_tree_length;
    char command_line[XNU_CMDLINE];
    uint64_t boot_flags, mem_size_actual;
};
_Static_assert(sizeof(struct xnu_boot_args) == 736, "XNU boot_args ABI size");
_Static_assert(offsetof(struct xnu_boot_args, device_tree) == 96, "XNU ADT pointer ABI");
_Static_assert(offsetof(struct xnu_boot_args, command_line) == 108, "XNU cmdline ABI");
struct xnu_payload {
    char magic[8];
    uint32_t version, segment_count;
    uint64_t virt_base, phys_base, mem_size, entry_va, kernel_end_pa;
    uint64_t segments_offset, data_offset, total_size;
    char command_line[XNU_CMDLINE];
};
struct xnu_segment { uint64_t physical, memory_size, file_offset, file_size; };
_Static_assert(sizeof(struct xnu_payload) == 688, "Payload header ABI");
struct handoff_plan { uint64_t adt_pa, args_pa, top, entry_pa; };
/* No writes to the proposed destination until the complete plan is accepted. */
int handoff_sparse(void *destination, const unsigned char *source, uint64_t length, uint64_t memory);
int handoff_validate(const void *payload, size_t length, uint64_t payload_pa,
                     const struct fdt_info *fdt, uint64_t fdt_pa,
                     uint64_t shim_start, uint64_t shim_end, struct handoff_plan *plan);
size_t xnu_make_adt(void *buffer, size_t capacity, const struct xnu_payload *payload,
                    uint32_t timebase);
size_t xnu_make_adt_seeded(void *buffer, size_t capacity, const struct xnu_payload *payload,
                           uint32_t timebase, const unsigned char *seed, size_t seed_bytes);
int mt8167_random_seed(unsigned char seed[64], uint32_t timebase);
const char *handoff_boot(const struct fdt_info *fdt, uint64_t fdt_pa);
#endif
