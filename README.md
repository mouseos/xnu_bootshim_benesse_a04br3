> **Work in progress / 作業中:** Experimental XNU port for Benesse TAB-A04-BR3 (MT8167). XNU entry has been observed, but the device currently watchdog-reboots; a working OS is not available.

# TAB-A04-BR3 XNU boot shim

First stage: a position-independent Cortex-A35 diagnostic payload entered by
the stock MediaTek LK. It supports EL1/EL2 entry, installs the corresponding
exception vectors and reports CPU feature registers over the inherited UART.
The second diagnostic stage drops from EL2 to EL1h with MMU/cache off, enables
EL1 architectural counter access, and inspects the LK-provided FDT for DRAM,
reserved ranges and initrd placement. If no XNU payload is supplied, it stops
after diagnostics. With a valid raw `A04XNU01` initrd payload it loads the
source-built kernel, creates Apple boot arguments and device tree, and jumps
to the Mach-O entry at EL1.
It performs no storage writes or watchdog changes.

The EL2 and EL1/FDT diagnostic stages were verified on TAB-A04-BR3.
The new kernel handoff has host validation only and awaits hardware verification.
DRAM ranges alone are not allocation permission: the loader also excludes FDT
reservations, its own code/stack, the FDT and the incoming payload.

Build on Linux with Python 3 and the GNU AArch64 toolchain:

```sh
./build.sh
```

Outputs go to `../out/bootshim` (override with `SHIM_OUT`). Wrap `Image` with
the sibling device repository's source-built DTB and boot-image packager.
No local boot image, firmware dump or generated DTB is required as an input.

Hardware acceptance: the existing UART capture must contain
`XNU_A04BR3_SHIM_DIAGNOSTIC_V1`, the register values and
`SHIM_DIAGNOSTIC_DONE`. Stage 2 additionally requires
`XNU_A04BR3_EL1_DIAGNOSTIC_V2`, `CurrentEL=0x0000000000000004`,
FDT memory/reserved information, and `SHIM_EL1_DIAGNOSTIC_DONE`.
A successful host build is not a successful device boot.
The inherited watchdog may reset the device after the diagnostic stops.

See [CREDITS.md](CREDITS.md) for source provenance. This loader is independent
of the intended iOS or macOS userland. Timer, interrupt and storage drivers in
the kernel remain separate work.

Host parser tests (requires a C compiler, dtc and Python):

```sh
python3 -m unittest discover -s tests -v
```

## Source-built kernel handoff

The device repository's `tools/payload.py` creates the raw payload from a
plain ARM64 executable Mach-O built from the kernel repository. No Apple
userland image is an input. Maximum input size is 128 MiB; the device GPT and
image configuration must provide enough space before flashing a larger image.

`handoff.h` defines the versioned wire header and segment descriptors. All
input bounds, destination windows, reservations, segment overlap and entry
placement are checked before copying. Segment file bytes are copied and BSS
is zeroed. `adt.c` serializes the board identity, boot CPU, UART and selected
memory window. No entropy or additional CPU support is fabricated. The XNU
boot_args structure is checked for the expected field offsets at compile time.

Hardware acceptance for this new stage requires the kernel's own
`XNU_A04BR3_KERNEL_ENTRY` UART marker after `XNU_HANDOFF_JUMP`. A shim jump
message alone does not establish kernel execution. Kernel entry does not
establish working memory management, interrupts, storage or userland.

The kernel handoff now requests a fresh 64-byte seed from MT8167 TRNG on each
boot and publishes `/chosen/random-seed`. No seed is stored in the image or
logs. The loader restores the TRNG enable and clock-gate state and clears its
copies after ADT transfer. A timeout, inaccessible device or stuck output fails
closed with an explicit marker. This hardware path is awaiting validation and
is not a certification of the vendor RNG's statistical quality.

Payload version 2 keeps the version 1 header and adds a final load segment
for the root RAMDisk. The loader validates the segment and reserves it below
ADT/boot_args, then publishes its physical address and padded length as
`/chosen/memory-map/RAMDisk`. Version 1 without a root disk remains supported.
