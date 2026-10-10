#!/usr/bin/env bash
# Run from the repo folder. 1) frames + audio, 2) 4K video, 3) 1080p video.
# Needs about 300 GB free. If it stops, run it again: finished frames are skipped.
set -e
OUT=out_$(git rev-parse --short HEAD)

# 1. 4K60 frames and cathedra_48k.wav
./build/cathedra_dev --export $OUT
cd $OUT

# 2. 4K60 archival / YouTube
ffmpeg -framerate 60 -i frame_%06d.png -i cathedra_48k.wav \
  -vf "zscale=matrixin=input:matrix=709:rangein=full:range=limited,format=yuv420p10le" \
  -c:v libsvtav1 -preset 4 -crf 18 -g 120 \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
  -c:a aac -b:a 384k -shortest -movflags +faststart \
  cathedra_4k60.mp4

# 3. 1080p60 x264 for x.com and other sites
ffmpeg -framerate 60 -i frame_%06d.png -i cathedra_48k.wav \
  -vf "zscale=w=1920:h=1080:matrixin=input:matrix=709:rangein=full:range=limited,format=yuv420p" \
  -c:v libx264 -preset slow -crf 17 \
  -x264-params "colorprim=bt709:transfer=bt709:colormatrix=bt709:range=tv" \
  -c:a aac -b:a 256k -shortest -movflags +faststart \
  cathedra_1080p60.mp4
