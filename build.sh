#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
src=$(cd "$(dirname "$0")" && pwd)
out=${SHIM_OUT:-$src/../out/bootshim}
prefix=${CROSS_COMPILE:-aarch64-linux-gnu-}
mkdir -p "$out"
flags=(-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align
       -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
       -mcpu=cortex-a35 -Wall -Wextra -Werror)
"${prefix}gcc" "${flags[@]}" -c "$src/start.S" -o "$out/start.o"
"${prefix}gcc" "${flags[@]}" -c "$src/shim.c" -o "$out/shim.o"
"${prefix}gcc" "${flags[@]}" -c "$src/fdt.c" -o "$out/fdt.o"
if "${prefix}readelf" -r "$out/shim.o" "$out/fdt.o" | grep -q 'R_AARCH64_ABS'; then
    echo 'Absolute C relocation is incompatible with a relocated shim' >&2; exit 1
fi
"${prefix}gcc" "${flags[@]}" -static -T "$src/shim.ld" -Wl,--build-id=none \
    -Wl,-N "$out/start.o" "$out/shim.o" "$out/fdt.o" -o "$out/shim.elf"
"${prefix}objcopy" -O binary "$out/shim.elf" "$out/Image"
"${prefix}nm" "$out/shim.elf" > "$out/symbols.txt"
python3 - "$out" <<'PY'
from pathlib import Path
import hashlib, json, struct, sys
p=Path(sys.argv[1]); syms={line.split()[2]:int(line.split()[0],16) for line in (p/'symbols.txt').read_text().splitlines() if len(line.split())==3}
image=p/'Image'
with image.open('r+b') as f: f.truncate(syms['_end'])
b=image.read_bytes()
assert b[56:60]==b'ARM\x64' and struct.unpack_from('<Q',b,16)[0]==len(b)
assert syms['_bss_end'] <= syms['_stack_top'] == len(b)
(p/'build.json').write_text(json.dumps({'kind':'diagnostic-shim','cpu':'cortex-a35','bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'xnu_boot_verified':False,'hardware_tested':False},indent=2)+'\n')
print(json.dumps({'image':str(image),'bytes':len(b),'hardware_tested':False}))
PY
