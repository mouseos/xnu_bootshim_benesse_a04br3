# Credits

The diagnostic shim is new source for the Benesse TAB-A04-BR3 port, copyright 2026 yuma shimizu, licensed under MIT.

Porting reference: dipa-studio/ios-sweet, https://github.com/dipa-studio/ios-sweet, commit `fdad0de8dc0e75c5ea84762adc03f7232dc91f19`. Its boot flow informed this work. The 64-byte entry header follows the Linux arm64 Image boot protocol. This repository does not import the upstream ios-sweet source tree or assign a license to it.

Hardware references: the MediaTek MT8167 platform sources and the existing A04BR3 EDK2 port. This diagnostic retains the bootloader UART configuration and does not incorporate their driver implementations.

Boot argument and Apple device-tree ABI reference: Apple's XNU release
`xnu-8796.141.3`, https://github.com/apple-oss-distributions/xnu/tree/xnu-8796.141.3
(`pexpert/pexpert/arm/boot.h` and platform device-tree interfaces). The loader,
validation and serializer implementations here are original port source.

MT8167 hardware RNG facts: MediaTek MT8167 LK `mt_reg_base.h` (TRNG base
0x1020c000), and MediaTek clock driver `clk-mt8167.c` (topckgen 0x10000000,
TRNG gate bit 5 in group 2). Public board-source reference:
https://github.com/s1204IT/CT3_GPL_LGPL_Src . Register control/ready/data layout
reference: Linux `drivers/char/hw_random/mtk-rng.c`, also present at
https://github.com/mouseos/android_kernel_benesse_TAB-A04-BR3 .
`rng.c` is original source, not an imported Linux driver. Hardware availability
and entropy quality still require runtime validation; failure does not produce
a synthetic seed.

The development /chosen/boot-manifest-hash namespace follows the mount
requirement documented by ios-sweet 0036 at fdad0de8dc0e75c5ea84762adc03f7232dc91f19.
The value is A04BR3-specific synthetic development data, not an Apple manifest.

Full donor roots use the explicit a04br3.full_root=1 development mode to
set only CSR_ALLOW_UNRESTRICTED_FS and the unauthenticated-root policy,
following the shared-cache condition documented in ios-sweet 0032. Linux
FUSE stat cannot preserve the donor native BSD SF_RESTRICTED inode flag.

Full-root memory profile is derived from A04BR3 hardware FDT observations:
RAM 40000000..80000000 and 80000000..be9c0000. The managed interval
60000000..be000000 spans adjacent banks and excludes every reported
reservation. Payload is loaded below this interval. Gaps, overlap with
reservations and overlap with firmware payloads still reject the handoff.

The opt-in LK display probe uses the 800x1280, 3200-byte stride,
0xbe9c0000 three-page scanout layout from mouseos/edk2-mtk A04BR3
DisplayInitDxe and a04br3.dsc in the existing local EDK2 port. It only
writes a 24-row RGB marker inside the FDT-reserved framebuffer and supplies
Apple boot_args Video; it does not initialize DSI, implement IOSurface or
claim SpringBoard output.

Correction after reading SimpleFbDxe: PcdMipiFrameBufferLayers=3 is
an inclusive upper index (four surfaces); every diagnostic color now
has alpha=0xff. The reserved extent validation covers all four surfaces.

Native display enumeration and chosen display-scale/rotation metadata follow
the ios-sweet 0046 source and bring-up notes at
fdad0de8dc0e75c5ea84762adc03f7232dc91f19 (dipa-studio/ios-sweet).
A04BR3 uses its real 800x1280 geometry and explicit 1:1 logical scale.
