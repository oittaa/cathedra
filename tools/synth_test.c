/* Offline test: render the soundtrack to a WAV + print timing and sync stats. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "../src/synth.h"
int main(int argc, char **argv) {
  struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
  synth_render();
  clock_gettime(CLOCK_MONOTONIC, &b);
  printf("synth: %.3f s for %d s of audio\n", (b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)*1e-9, SONG_SEC);
  int peak = 0, clip = 0; double rms = 0;
  for (int i = 0; i < NFR*2; i++) { int v = abs(pcm[i]); if (v > peak) peak = v; if (v > 29000) clip++; rms += (double)pcm[i]*pcm[i]; }
  printf("peak %d  near-clip %d  rms %.0f\n", peak, clip, sqrt(rms/(NFR*2)));
  for (int s = 0; s < 10; s++) { double r = 0; for (int i = s*16*SR; i < (s+1)*16*SR; i++) r += (double)pcm[2*i]*pcm[2*i]; printf("sec %d rms %.0f\n", s, sqrt(r/(16*SR))); }
  FILE *f = fopen(argc > 1 ? argv[1] : "cathedra.wav", "wb");
  int data = NFR*4, hdr[11] = {0x46464952, 36+data, 0x45564157, 0x20746d66, 16, 0x00020001, SR, SR*4, 0x00100004, 0x61746164, data};
  fwrite(hdr, 4, 11, f); fwrite(pcm, 2, NFR*2, f); fclose(f);
  return 0;
}
