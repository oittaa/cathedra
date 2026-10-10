#!/usr/bin/env bash
# CATHEDRA - Linux build (Ubuntu 24.04/26.04, Debian, any X11/XWayland + ALSA system)
#   ./build_linux.sh            release (packed 64k entry) + dev build
#   ./build_linux.sh release    release only
#   ./build_linux.sh dev        dev build only (all switches, selftest, export)
#   ./build_linux.sh debug      dev build with -O0 -g and warnings
# deps: sudo apt install build-essential libx11-dev libasound2-dev xz-utils (+ upx-ucl optional)
# optional (to regenerate SPIR-V): glslang-tools spirv-tools python3
set -euo pipefail
cd "$(dirname "$0")"
MODE=${1:-all}
CC=${CC:-cc}
INCLUDES="-Isrc/third_party"
LIBS="-lX11 -lasound -lm -lpthread -ldl"
mkdir -p build
# SPIR-V is committed (src/spirv.h); regenerate only if the tools are present
if command -v python3 >/dev/null 2>&1 && command -v glslangValidator >/dev/null 2>&1 && command -v spirv-opt >/dev/null 2>&1 && command -v spirv-val >/dev/null 2>&1; then
  python3 tools/build_spirv.py >/dev/null
fi

if [ "$MODE" = debug ]; then
  $CC -g -O0 -Wall -Wextra -Wno-missing-field-initializers -DCATHEDRA_DEV $INCLUDES -o build/cathedra_debug src/main.c $LIBS
  echo "built build/cathedra_debug"; exit 0
fi

if [ "$MODE" = all ] || [ "$MODE" = dev ]; then
  $CC -O2 -DCATHEDRA_DEV $INCLUDES -o build/cathedra_dev src/main.c $LIBS
  echo "dev ELF      : $(stat -c %s build/cathedra_dev) bytes  (build/cathedra_dev)"
fi
[ "$MODE" = dev ] && exit 0

# -Oz without post-RA scheduling / peephole2: regular instruction patterns pack ~1.3 KB smaller.
# plus a search of size-neutral-in-x86 / better-packing switches (no inlining of
# called-once functions, no caller-saves, no loop-invariant motion / loop opts, no tail merging,
# priority IRA, no pure-const IPA, no FRE): ~950 bytes less after LZMA. No _FORTIFY_SOURCE (Ubuntu's
# gcc default): its __memcpy_chk / __fprintf_chk wrappers cost ~200 bytes in the 64k
# No C runtime startup files (-nostartfiles): main.c has its own _start.
# No CET endbr64 on every function (-fcf-protection=none, also an Ubuntu default).
# SSE4.1: floorf/truncf as one instruction, no libm call.
FLAGS="-Oz -msse4.1 -fcf-protection=none -U_FORTIFY_SOURCE -fno-inline-functions-called-once -fno-caller-saves -fno-move-loop-invariants -fno-tree-loop-optimize -fno-tree-tail-merge -fira-algorithm=priority -fno-ipa-pure-const -fno-tree-fre -fno-schedule-insns2 -fno-peephole2 -ffast-math -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
 -ffile-prefix-map=$PWD=. -fno-ident -fno-plt -ffunction-sections -fdata-sections -fno-pie -no-pie \
 -nostartfiles -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,norelro -Wl,-z,noseparate-code -Wl,--hash-style=gnu -Wl,-O1 -s"
$CC $FLAGS $INCLUDES -o build/cathedra_unpacked src/main.c $LIBS
if command -v upx >/dev/null 2>&1; then
  rm -f build/cathedra_upx
  upx -q --best --lzma -o build/cathedra_upx build/cathedra_unpacked >/dev/null && \
    echo "UPX ELF      : $(stat -c %s build/cathedra_upx) bytes  (build/cathedra_upx, alternative)"
fi

strip -s -R .comment -R .note.gnu.property -R .note.ABI-tag build/cathedra_unpacked 2>/dev/null || true
echo "unpacked ELF : $(stat -c %s build/cathedra_unpacked) bytes  (build/cathedra_unpacked)"

# classic demoscene shell dropper: 2-line sh header + raw LZMA stream (needs /bin/sh, tail, xz:
# present on every Ubuntu install). x86 BCJ filter (call/jmp targets made absolute) + LZMA1
# with lc=1/pb=0 for the mix of x86 code and repacked SPIR-V; raw format = no 13-byte header.
# the encoder-only knobs (match finder, nice length, search depth) are searched per build
# (the decoder only needs lc/lp/pb): typically 40-70 bytes below the fixed bt2 setting
best=
for mf in bt2 bt3 bt4; do for nice in 64 96 128 192 273; do for depth in 0 1000; do
  xz --format=raw --x86 --lzma1=preset=9e,lc=1,lp=0,pb=0,mf=$mf,nice=$nice,depth=$depth -c build/cathedra_unpacked > build/try.lzma
  n=$(stat -c %s build/try.lzma); if [ -z "$best" ] || [ "$n" -lt "$best" ]; then best=$n; mv build/try.lzma build/payload.lzma; fi
done; done; done
rm -f build/try.lzma
printf '#!/bin/sh\na=${XDG_RUNTIME_DIR:-/tmp}/cathedra$$;trap "rm -f $a" 0;trap exit 1 2 15;tail -n+3 "$0"|xz -dcFraw --x86 --lzma1=lc=1,pb=0>$a;chmod +x $a;$a "$@"\n' > build/cathedra
cat build/payload.lzma >> build/cathedra
chmod +x build/cathedra
rm -f build/payload.lzma
SZ=$(stat -c %s build/cathedra)
echo "LZMA dropper : $SZ bytes  (build/cathedra)  <- the 64k entry"
if [ "$SZ" -gt 65536 ]; then echo "ERROR: over 65536 bytes"; exit 1; fi
