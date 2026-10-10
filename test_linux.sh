#!/usr/bin/env bash
# CATHEDRA - native Linux test (Ubuntu, Nvidia driver), uses the DEV build.
#   ./test_linux.sh                 build (if needed), selftest log + shots at the reference times
#   ./test_linux.sh -sweep          also the per-second GPU timing sweep (warm, no PNG writes in the loop)
#   ./test_linux.sh -nancheck       also the NaN/Inf + black-tile detector over the finale (140-175 s, every
#                                   frame, 1080p, counted on the GPU); exit code 3 on failure
#                                   (log: test_out/nancheck/cathedra_selftest.log)
#   ./test_linux.sh -lumacheck      also the flash detector over the whole film (0-176 s, every frame at
#                                   60 fps, mean luma on the GPU, spikes + exposure pulses); exit code 4
#                                   on failure (log + lumacheck.csv in test_out/lumacheck)
#   ./test_linux.sh -export 2       also a 2-second 4K/60 YouTube export smoke test (16-bit PNG + WAV)
#   ./test_linux.sh -rebuild        force a rebuild first (needs build-essential libx11-dev libasound2-dev xz-utils)
#   ./test_linux.sh -rtcheck        also the real-time start simulation (0-4 s: wall clock, 60 Hz pacing, probe scaler
#                                   supersampling + render-target reallocation exactly as the release; test_out/rtcheck)
#   ./test_linux.sh -all            = -sweep -nancheck -lumacheck -rtcheck
# Options combine: ./test_linux.sh -sweep -nancheck
set -uo pipefail
cd "$(dirname "$0")"
SWEEP=0; NAN=0; LUMA=0; RT=0; EXPORT=0; REBUILD=0
while [ $# -gt 0 ]; do
  case "$1" in
    -sweep|--sweep|-Sweep) SWEEP=1 ;;
    -nancheck|--nancheck|-NanCheck) NAN=1 ;;
    -lumacheck|--lumacheck|-LumaCheck) LUMA=1 ;;
    -export|--export|-Export) EXPORT="$2"; shift ;;
    -rtcheck|--rtcheck) RT=1 ;;
    -all|--all) SWEEP=1; NAN=1; LUMA=1; RT=1 ;;
    -rebuild|--rebuild) REBUILD=1 ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac
  shift
done
if [ $REBUILD = 1 ] || [ ! -x build/cathedra_dev ]; then ./build_linux.sh || exit 1; fi
mkdir -p test_out
cd test_out
ARGS=(--selftest)
for t in 6 22 28 40 60 90 101 120 136 146 152 158 166 170; do ARGS+=(--shot "$t"); done
[ $SWEEP = 1 ] && ARGS+=(--sweep)
../build/cathedra_dev "${ARGS[@]}"
ST=$?
echo "selftest exit code $ST - see test_out/cathedra_selftest.log and test_out/shot_*.png"
if [ $NAN = 1 ]; then
  mkdir -p nancheck
  (cd nancheck && ../../build/cathedra_dev --nancheck 140 175 --frames 0)
  NC=$?
  if [ $NC = 0 ]; then echo "nancheck PASS (140-175 s) - see test_out/nancheck/cathedra_selftest.log"
  else echo "nancheck FAIL (exit code $NC) - see test_out/nancheck/cathedra_selftest.log"; fi
  [ $ST = 0 ] && ST=$NC
fi
if [ $LUMA = 1 ]; then
  mkdir -p lumacheck
  (cd lumacheck && ../../build/cathedra_dev --lumacheck 0 176 --frames 0)
  LC=$?
  if [ $LC = 0 ]; then echo "lumacheck PASS (0-176 s) - see test_out/lumacheck/cathedra_selftest.log"
  else echo "lumacheck FAIL (exit code $LC) - see test_out/lumacheck/cathedra_selftest.log and lumacheck.csv"; fi
  [ $ST = 0 ] && ST=$LC
fi
if [ $RT = 1 ]; then
  mkdir -p rtcheck
  (cd rtcheck && ../../build/cathedra_dev --rtcheck 4 --frames 0)
  RC=$?
  if [ $RC = 0 ]; then echo "rtcheck PASS (real-time start 0-4 s) - see test_out/rtcheck/cathedra_selftest.log"
  else echo "rtcheck FAIL (exit code $RC) - see test_out/rtcheck/cathedra_selftest.log and rtcheck.csv"; fi
  [ $ST = 0 ] && ST=$RC
fi
if [ "$EXPORT" != 0 ]; then
  ../build/cathedra_dev --export export_smoke --from 100 --to "$(awk "BEGIN{print 100+$EXPORT}")"
  echo "export: test_out/export_smoke (frame_*.png, cathedra.wav, export_info.txt)"
fi
exit $ST
