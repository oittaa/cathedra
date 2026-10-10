# CATHEDRA - reproducible Linux release build: same inputs -> bit-identical build/cathedra.
#   ./build_docker.sh      (docker build, copies build/cathedra and build/cathedra_dev out, checks SHA256SUMS)
# Pinned: the base image by digest; every package through an immutable snapshot of the Ubuntu
# archive (snapshot.ubuntu.com, ARG SNAPSHOT) plus explicit versions of the toolchain.
FROM ubuntu:26.04@sha256:f144425ff09be612d6d9ad965196e9cdc23dae1f42110a8a11a3e9a8198759f7

ARG SNAPSHOT=20261009T000000Z
# used automatically when the snapshot can't serve the pinned versions (same versions, same sha256s)
ARG MIRROR=https://archive.ubuntu.com/ubuntu/
ENV LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH=1791504000 PYTHONHASHSEED=0 PYTHONDONTWRITEBYTECODE=1 \
    DEBIAN_FRONTEND=noninteractive

# Toolchain: docker/apt.sh - CA bundle first (all https verified), snapshot with retries/backoff,
# fallback MIRROR, every .deb checked against docker/debs.lock (sha256) before it is installed.
COPY docker/apt.sh docker/debs.lock /tmp/
RUN sh /tmp/apt.sh "$SNAPSHOT" "$MIRROR"

# fixed build path; only the sources go in (see .dockerignore), copied in a fixed order
WORKDIR /build/cathedra
COPY build_linux.sh ./
COPY tools/ tools/
COPY src/ src/
# SPIR-V is regenerated from src/shaders with the pinned glslang/spirv-tools (no randomness:
# fixed pass list, sorted names, PYTHONHASHSEED=0); the LZMA search is a fixed, ordered loop
# with single-threaded xz. Compile flags include -ffile-prefix-map and --build-id=none.
RUN rm -f src/spirv.h build/* \
 && ./build_linux.sh \
 && sha256sum build/cathedra src/spirv.h
