# Credits

The diagnostic shim is new source for the Benesse TAB-A04-BR3 port, copyright 2026 yuma shimizu, licensed under MIT.

Porting reference: dipa-studio/ios-sweet, https://github.com/dipa-studio/ios-sweet, commit `fdad0de8dc0e75c5ea84762adc03f7232dc91f19`. Its boot flow informed this work. The 64-byte entry header follows the Linux arm64 Image boot protocol. This repository does not import the upstream ios-sweet source tree or assign a license to it.

Hardware references: the MediaTek MT8167 platform sources and the existing A04BR3 EDK2 port. This diagnostic retains the bootloader UART configuration and does not incorporate their driver implementations.
