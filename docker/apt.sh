#!/bin/sh
# CATHEDRA reproducible build: install the pinned toolchain (run inside the Dockerfile).
#   apt.sh SNAPSHOT MIRROR          (GENLOCK=1: print a new debs.lock instead of installing)
# 1. bootstrap ca-certificates from the image's own sources (http; apt checks the signed InRelease),
#    so every later https connection is verified (no Verify-Peer=false)
# 2. sources = the immutable snapshot (https); if it can't serve the pinned versions after apt's
#    retries with exponential backoff, the fallback MIRROR (https, verified) is used instead
# 3. the .debs are downloaded first (a source that fails mid-download hands over to the next) and checked against docker/debs.lock (sha256 of every package
#    file), so whichever source served them, the installed bytes are the pinned ones; then installed
set -eu
SNAP=$1; MIRROR=$2
PKGS="gcc=4:15.2.0-5ubuntu1 gcc-15=15.2.0-16ubuntu1 cpp=4:15.2.0-5ubuntu1 binutils=2.46-3ubuntu2
 libc6-dev=2.43-2ubuntu2.4 libx11-dev=2:1.8.13-1 libasound2-dev=1.2.15.3-1ubuntu1.1 xz-utils=5.8.3-1
 python3=3.14.3-0ubuntu2 glslang-tools=16.2.0-2 spirv-tools=2026.1-1"
cat > /etc/apt/apt.conf.d/80retries <<'C'
Acquire::Retries "3";
Acquire::Retries::Delay "true";
Acquire::Retries::Delay::Maximum "16";
Acquire::http::Timeout "60";
Acquire::https::Timeout "60";
C
# 1. CA bundle (not part of the toolchain: it only makes TLS verifiable)
apt-get update
apt-get install -y --no-install-recommends ca-certificates
src() {
  cat > /etc/apt/sources.list.d/ubuntu.sources <<S
Types: deb
URIs: $1
Suites: resolute resolute-updates resolute-security
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
S
  rm -rf /var/lib/apt/lists/*
  apt-get update || return 1
  # the pinned toolchain must be resolvable from this source
  apt-get install -s --no-install-recommends $PKGS >/dev/null
}
# 2+3a. snapshot, then the fallback mirror (each with apt's retries/backoff; the pair is tried twice).
# A source must give both the index and the .debs; files it did fetch stay in the cache (they are
# checked against the lock anyway), the next source only fetches the rest.
apt-get clean
ok=
for u in "https://snapshot.ubuntu.com/ubuntu/$SNAP/" "$MIRROR" "https://snapshot.ubuntu.com/ubuntu/$SNAP/" "$MIRROR"; do
  if src "$u" && apt-get install -y --no-install-recommends --download-only $PKGS; then ok=$u; break; fi
  echo "apt.sh: source $u failed, next" >&2; sleep 10
done
[ -n "$ok" ] || { echo "apt.sh: no source serves the pinned versions" >&2; exit 1; }
echo "apt.sh: index + remaining packages from $ok"
# 3b. verify against the lock, install from the cache
cd /var/cache/apt/archives
if [ "${GENLOCK:-}" = 1 ]; then sha256sum $(ls *.deb | sort); exit 0; fi   # regenerate docker/debs.lock
ls *.deb | sort > /tmp/got
cut -c67- /tmp/debs.lock | sort > /tmp/want
if ! cmp -s /tmp/got /tmp/want; then echo "apt.sh: package set differs from docker/debs.lock:" >&2; diff /tmp/want /tmp/got >&2; exit 1; fi
sha256sum -c --quiet /tmp/debs.lock
apt-get install -y --no-install-recommends $PKGS
rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*.deb
