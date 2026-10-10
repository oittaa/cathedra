#!/usr/bin/env bash
# CATHEDRA - reproducible release build in Docker (see Dockerfile). Writes build/cathedra
# (+ build/cathedra_unpacked, build/cathedra_dev = the dev build of the same sources (runs on the
# host: same glibc), and build/spirv.docker.h = the SPIR-V header it was built with;
# the kit commits that header as src/spirv.h, so the dev build runs the same shaders)
# and checks it against SHA256SUMS.
#   ./build_docker.sh            cached build (reuses the toolchain layer; same output)
#   ./build_docker.sh --clean    from scratch: --no-cache (no --pull: the base is pinned by digest)
#   DOCKER=podman ./build_docker.sh
#   MIRROR=<ubuntu mirror URL> ./build_docker.sh   (fallback source for the pinned .debs)
set -euo pipefail
cd "$(dirname "$0")"
D=${DOCKER:-docker}
IMG=cathedra-build:1.0
OPT=()
for a in "$@"; do
  case "$a" in
    --clean) OPT+=(--no-cache --pull=false) ;;
    *) echo "usage: $0 [--clean]"; exit 2 ;;
  esac
done
$D build "${OPT[@]}" ${MIRROR:+--build-arg MIRROR=$MIRROR} -t $IMG .
id=$($D create $IMG)
trap '$D rm -f $id >/dev/null' EXIT
mkdir -p build
$D cp $id:/build/cathedra/build/cathedra build/cathedra
$D cp $id:/build/cathedra/build/cathedra_unpacked build/cathedra_unpacked
$D cp $id:/build/cathedra/build/cathedra_dev build/cathedra_dev
$D cp $id:/build/cathedra/src/spirv.h build/spirv.docker.h
SZ=$(stat -c %s build/cathedra)
echo "build/cathedra: $SZ bytes ($((65536-SZ)) free), sha256 $(sha256sum build/cathedra | cut -d' ' -f1)"
[ "$SZ" -le 65536 ] || { echo "ERROR: over 65536 bytes"; exit 1; }
if [ -f SHA256SUMS ]; then sha256sum -c SHA256SUMS; fi
