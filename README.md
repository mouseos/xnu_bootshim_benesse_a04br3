# TAB-A04-BR3 XNU boot shim

First stage: a position-independent Cortex-A35 diagnostic payload entered by
the stock MediaTek LK. It supports EL1/EL2 entry, installs the corresponding
exception vectors and reports CPU feature registers over the inherited UART.
The second diagnostic stage drops from EL2 to EL1h with MMU/cache off, enables
EL1 architectural counter access, and inspects the LK-provided FDT for DRAM,
reserved ranges and initrd placement. It then stops without loading XNU.
It performs no storage writes or watchdog changes.

The EL2 stage was verified on TAB-A04-BR3. The EL1/FDT stage requires its own
hardware verification. The parser only reports ranges; its output is not yet
an allocator or a validated XNU memory map.

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

See [CREDITS.md](CREDITS.md) for source provenance. Future work adds the
LK-to-XNU handoff without tying this loader to iOS or macOS userland.

Host parser tests (requires a C compiler, dtc and Python):

```sh
python3 -m unittest discover -s tests -v
```
