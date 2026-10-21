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

Version 3 encodes only the final RAMDisk segment as little-endian 32-bit
runs: bit 31 selects zero fill, bits 30..0 give a nonzero length; literal
runs carry that many following bytes. The entire stream must decode to the
segment memory size before the loader writes any destination. Kernel
segments keep their version 1 layout. This stores sparse HFS volumes within
the existing boot partition without modifying GPT.

Version 5 adds exactly two raw tail segments after all kernel segments: WMT
then GPU. It requires a single `rd=emmcroot0` selector and forbids RAMDisk
interpretation. Both assets start on 16KiB boundaries, use exact rounded
reservations, are excluded from entry selection, and must form a contiguous
physical tail below 4GiB. Their source offsets must immediately follow the
kernel and each other with eight-byte file padding, with no extra tail bytes.
GPU remains last, preserving the v4 kernel GPU reservation invariant. ADT
publishes actual lengths as `WMTFirmware-A04BR3` and `GPUFirmware-A04BR3` under
`/chosen/memory-map`. The shim treats these as opaque bounded assets; the
source device packer and kernel WMT parser validate the A04WMT1 contents.
Formats v1 through v4 are unchanged.


Payload v6 extends the explicit eMMC-root asset tail to WLAN, WMT, GPU. All
three must occupy consecutive 16KiB-rounded physical reservations after the
kernel, with exact padded source ranges, no entry point in any asset, and no
extra trailer. WLAN input is bounded to 16MiB; WMT/GPU retain their previous
bounds. `WLANFirmware-A04BR3` publishes the actual WLAN byte count. This is an
opaque boot asset, never a RAMDisk or a claim of device firmware execution.

The v6 WLAN profile also describes the physical MT6630 SDIO function1 as
`/arm-io/wlan`, with MTK compatible identity and MSDC2 host address. Native
wifid uses device-tree presence as a launch prerequisite. This descriptor
contains no MAC, calibration bytes or firmware-ready/connected claim. Earlier
payload profiles do not publish the WLAN entry.
