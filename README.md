# TAB-A04-BR3 XNU boot shim

First stage: a position-independent Cortex-A35 diagnostic payload entered by
the stock MediaTek LK. It supports EL1/EL2 entry, installs the corresponding
exception vectors and reports CPU feature registers over the inherited UART.
It then stops. It does not load XNU yet. No storage writes or watchdog changes.

Build on Linux with Python 3 and the GNU AArch64 toolchain:

```sh
./build.sh
```

Outputs go to `../out/bootshim` (override with `SHIM_OUT`). Wrap `Image` with
the sibling device repository's boot-image packager and a locally supplied
working boot image. No firmware dumps are stored here.

Hardware acceptance: the existing UART capture must contain
`XNU_A04BR3_SHIM_DIAGNOSTIC_V1`, the register values and
`SHIM_DIAGNOSTIC_DONE`. A successful host build is not a successful device boot.
The inherited watchdog may reset the device after the diagnostic stops.

See [CREDITS.md](CREDITS.md) for source provenance. Future work adds the
LK-to-XNU handoff without tying this loader to iOS or macOS userland.
