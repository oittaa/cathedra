#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
/* CATHEDRA - developer tooling, compiled only into cathedra_dev (-DCATHEDRA_DEV):
 * command line, self-test with timing sweep and logs, PNG shots, raw frame capture. */
/* minimal PNG writer (stored deflate blocks) for the self-test screenshots */
static unsigned crc_t[256];
static unsigned crc(unsigned c, const unsigned char *p, size_t n) {
  if (!crc_t[1]) for (unsigned i = 0; i < 256; i++) { unsigned k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_t[i] = k; }
  c = ~c; while (n--) c = crc_t[(c ^ *p++) & 255] ^ (c >> 8); return ~c;
}
static void be32(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *t, const unsigned char *d, unsigned n) {
  unsigned char h[8]; be32(h, n); memcpy(h + 4, t, 4); fwrite(h, 1, 8, f); fwrite(d, 1, n, f);
  unsigned c = crc(crc(0, (const unsigned char *)t, 4), d, n); be32(h, c); fwrite(h, 1, 4, f);
}
static void write_png(const char *fn, const unsigned char *rgb, int w, int h) {
  size_t rl = (size_t)w * 3 + 1, n = rl * h, nb = (n + 65534) / 65535, a = 1, b = 0;
  unsigned char *z = (unsigned char *)malloc(n + nb * 5 + 6), *q = z, ih[13];
  *q++ = 0x78; *q++ = 1;
  for (size_t i = 0; i < n; ) {
    size_t k = n - i < 65535 ? n - i : 65535;
    *q++ = i + k == n; q[0] = k & 255; q[1] = k >> 8; q[2] = ~k & 255; q[3] = (~k >> 8) & 255; q += 4;
    for (size_t j = 0; j < k; j++, i++) {
      unsigned char v = i % rl ? rgb[(i / rl) * w * 3 + i % rl - 1] : 0;
      *q++ = v; a = (a + v) % 65521; b = (b + a) % 65521;
    }
  }
  be32(q, (unsigned)(b << 16 | a)); q += 4;
  FILE *f = fopen(fn, "wb"); if (!f) { free(z); return; }
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
  chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (unsigned)(q - z)); chunk(f, "IEND", 0, 0);
  fclose(f); free(z);
}

static const char *exp_dir; static int exp_fps = 60, exp_bits = 16, exp_spp = 32, exp_wav = 32, exp_f0 = -1, exp_f1 = -1, exp_redo, exp_nowav; static float exp_from = 0, exp_to = SONG_SEC;
static int dev_export(float scale);
static float rt_t0 = 0, rt_t1 = -1, nan_t0 = -1, nan_t1, luma_t0 = -1, luma_t1; static float shots[64], raw_fps, raw_t0, raw_t1 = SONG_SEC; static int still, nshots, frames = -1, selftest, taa_lead = 8, sweep, size_set;
static void dev_args(int argc, char **argv, float *fixed_scale) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i], *b = i + 1 < argc ? argv[i + 1] : "0";
    if (!strcmp(a, "-w")) windowed = 1;
    else if (!strcmp(a, "-s")) { *fixed_scale = num(b); i++; }
    else if (!strcmp(a, "--selftest")) selftest = 1;
    else if (!strcmp(a, "--shot") && nshots < 64) { shots[nshots++] = num(b); selftest = 1; i++; }
    else if (!strcmp(a, "--frames")) { frames = (int)num(b); selftest = 1; i++; }
    else if (!strcmp(a, "--size")) { size_set = 1; W = (int)num(b); H = (int)num(strchr(b, 'x') ? strchr(b, 'x') + 1 : b); i++; }
    else if (!strcmp(a, "--still")) still = 1;   /* --raw at a frozen instant: only the TAA/sample jitter moves */
    else if (!strcmp(a, "--raw") && i + 3 < argc) { raw_fps = num(b); raw_t0 = num(argv[i + 2]); raw_t1 = num(argv[i + 3]); i += 3; }
    else if (!strcmp(a, "--lumacheck") && i + 2 < argc) { luma_t0 = num(b); luma_t1 = num(argv[i + 2]); selftest = 1; i += 2; }
    else if (!strcmp(a, "--rtcheck")) { rt_t1 = num(b); selftest = 1; i++; }
    else if (!strcmp(a, "--rtfrom")) { rt_t0 = num(b); i++; }   /* --rtcheck start time (default 0) */   /* real-time playback simulation from t=0 */
    else if (!strcmp(a, "--nancheck") && i + 2 < argc) { nan_t0 = num(b); nan_t1 = num(argv[i + 2]); selftest = 1; i += 2; }
    else if (!strcmp(a, "--tier")) { tier = (int)num(b); tier = tier > 3 ? 3 : tier; i++; }
    else if (!strcmp(a, "--taa")) { taa_lead = (int)num(b); i++; }
    else if (!strcmp(a, "--sweep")) { sweep = 1; selftest = 1; }
    else if (!strcmp(a, "--export")) { exp_dir = b; i++; }
    else if (!strcmp(a, "--fps")) { exp_fps = (int)num(b); i++; }
    else if (!strcmp(a, "--from")) { exp_from = num(b); i++; }
    else if (!strcmp(a, "--to")) { exp_to = num(b); i++; }
    else if (!strcmp(a, "--section")) { int n = (int)num(b); n = n < 0 ? 0 : n > 10 ? 10 : n; exp_from = n * 16.f; exp_to = n * 16.f + 16; i++; }
#ifndef _WIN32
    else if (!strcmp(a, "--loop")) play_loop = 1;
#endif
    else if (!strcmp(a, "--bits")) { exp_bits = (int)num(b) == 8 ? 8 : 16; i++; }
    else if (!strcmp(a, "--spp")) { exp_spp = (int)num(b); if (exp_spp < 1) exp_spp = 1; i++; }
    else if (!strcmp(a, "--wav")) { exp_wav = (int)num(b) == 24 ? 24 : 32; i++; }
    else if (!strcmp(a, "--first")) { exp_f0 = (int)num(b); i++; }   /* export: frame numbers (inclusive), instead of --from/--to */
    else if (!strcmp(a, "--last")) { exp_f1 = (int)num(b); i++; }
    else if (!strcmp(a, "--redo")) exp_redo = 1;                      /* export: render frames that are already there again */
    else if (!strcmp(a, "--nowav")) exp_nowav = 1;                    /* export: no cathedra_48k.wav (the synth still runs: the picture syncs to it) */
  }
  if (exp_to > SONG_SEC) exp_to = SONG_SEC;
  if (exp_from < 0) exp_from = 0;
  if (exp_from > exp_to - .1f) exp_from = exp_to - .1f;   /* playback and export: a non-empty range */
  if (exp_dir) {   /* offline export: 4K60 by default, everything maxed, nothing adaptive */
    if (!size_set) { W = 3840; H = 2160; }
    if (exp_to > SONG_SEC) exp_to = SONG_SEC;
    if (tier < 0) tier = 3;
    pcmf = (float *)malloc(sizeof(float) * 2 * NFR);
    headless = 1; return;
  }
  headless = selftest || raw_fps > 0;
  if (headless && !size_set && !raw_fps) { W = 1920; H = 1080; }
  if (selftest && !nshots && !sweep && nan_t0 < 0 && rt_t1 < 0) { static const float d[] = { 6, 22, 28, 40, 60, 90, 101, 120, 136, 146, 152, 158, 170 }; nshots = 13; memcpy(shots, d, sizeof d); }
  if (frames < 0) frames = 16;
}
#ifndef _WIN32
/* windowed playback of a range (--from/--to/--section, --loop): jump the music and the frame clock.
 * The TAA history restarts by itself (next_frame: a time jump > .3 s), the scaler re-reads the section scale */
static int play_note = 1, play_loops; static double play_w0;
static void dev_seek(double s) {
  if (have_audio) { aseek = (int)(s * SR); for (int k = 0; k < 250 && aseek >= 0; k++) nap(); }
  t_last = 0; t_clock0 = now_sec() - s; t_pause_acc = 0; t_pause_at = now_sec();
  asec = -1; play_loops++; play_note = 1;
}
/* t < 0: start banner; otherwise one line for the first frame after the start or a loop jump */
static void dev_play_log(double t) {
  if (t < 0) { play_w0 = now_sec(); fprintf(stderr, "playback: %.2f-%.2f s%s, audio %s from frame %d (%.3f s)\n", exp_from, exp_to, play_loop ? ", looping" : "", have_audio ? "on" : "off", acur, (double)acur / SR); return; }
  if (!play_note) return;
  play_note = 0;
  fprintf(stderr, "playback%s at wall +%.2f s: first frame t=%.3f s, audio clock %.3f s (frame %d)\n", play_loops ? " loop" : "", now_sec() - play_w0, t, have_audio ? apos : t, acur);
}
#endif
static FILE *lg;
#define LOG(...) do { fprintf(stderr, __VA_ARGS__); if (lg) { fprintf(lg, __VA_ARGS__); fflush(lg); } } while (0)
static Img O; static Buf rb;
static void dev_frame(float t, float sc, int copy) {
  next_frame(t);
  record_frame(t, sc, O.i, O.v, L_SRC);
  if (copy) { VkBufferImageCopy cp = { 0, 0, 0, { COL, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)W, (uint32_t)H, 1 } }; vkCmdCopyImageToBuffer(cmd, O.i, L_SRC, rb.b, 1, &cp); }
  csubmit(0, 0); cwait(); read_ts(); frame_done(t);
}
/* NaN/Inf detector: every frame of [nan_t0, nan_t1) at 60 fps (continuous TAA history). A dev-only
 * compute shader (src/shaders/dev_nancheck.comp -> src/spirv_dev.h, not in the 64k) counts on the GPU
 * the fp16 NaN/Inf channels of the scene+swarm target (C), the TAA history (level 0) and every level
 * of its bloom/DOF mip chain, plus the 16x16 tiles of the final 8-bit frame that are exactly black
 * inside the letterbox (the visible symptom); only 4 counters are read back per frame, so it runs at
 * near real-time speed at 1080p. Any hit fails the self-test (exit 3). */
#include "spirv_dev.h"
static VkPipelineLayout nc_pl; static VkPipeline nc_pp; static VkDescriptorSet nc_ds[4]; static Buf nc_cb;
static void nc_setup(void) {
  if (nc_pp) return;
  VkDescriptorSetLayoutBinding lb[2] = { { 0, CIS, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0 }, { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0 } };
  VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, 2, lb };
  VkDescriptorSetLayout dl; CK(vkCreateDescriptorSetLayout(dev, &dli, 0, &dl));
  VkPushConstantRange pr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
  VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &dl, 1, &pr };
  VkPipelineLayout pl; CK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
  VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0,
    { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, shmod(spv_dev_nancheck, sizeof spv_dev_nancheck), "main", 0 }, pl, 0, -1 };
  VkPipeline pp; CK(vkCreateComputePipelines(dev, 0, 1, &ci, 0, &pp));
  VkDescriptorPoolSize ps[2] = { { CIS, 4 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 } };
  VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 4, 2, ps };
  VkDescriptorPool dp; CK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
  VkDescriptorSetLayout dls[4] = { dl, dl, dl, dl }; VkDescriptorSet ds[4];
  VkDescriptorSetAllocateInfo dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, dp, 4, dls };
  CK(vkAllocateDescriptorSets(dev, &dai, ds));
  Buf cb = mkbuf(64, 1);
  VkImageView vw[4] = { C.v, Hh[0].v, Hh[1].v, O.v };   /* sets: 0 scene, 1/2 history ping-pong, 3 final frame */
  for (int i = 0; i < 4; i++) {
    VkDescriptorImageInfo ii = { smp_clamp, vw[i], L_RO }; VkDescriptorBufferInfo bi = { cb.b, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, ds[i], 0, 0, 1, CIS, &ii, 0, 0 },
                                  { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, ds[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &bi, 0 } };
    vkUpdateDescriptorSets(dev, 2, w, 0, 0);
  }
  nc_pl = pl; nc_pp = pp; for (int i = 0; i < 4; i++) nc_ds[i] = ds[i]; nc_cb = cb;
}
#define NCD(set, lod, slot, mode, w, h) { int a_[4] = { lod, slot, mode, y0 }; vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, nc_pl, 0, 1, &nc_ds[set], 0, 0); \
  vkCmdPushConstants(cmd, nc_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, a_); vkCmdDispatch(cmd, ((w) + 15) / 16, ((h) + 15) / 16, 1); }
static int dev_nancheck(float sc) {
  nc_setup(); Buf cb = nc_cb;
  int nfr = (int)((nan_t1 - nan_t0) * 60), badf = 0; long tot[4] = { 0, 0, 0, 0 };
  int y0 = (int)(H * .5f * (1 - (W / 2.2f) / H)) + 2;
  LOG("NaN/Inf check t=%.1f-%.1f s, %d consecutive frames at 60 fps, %dx%d (render %dx%d), GPU-side counting\n", nan_t0, nan_t1, nfr, W, H, fw, fh);
  double c0 = now_sec();
  for (int i = 0; i < nfr; i++) {
    float t = nan_t0 + i / 60.f;
    next_frame(t);
    record_frame(t, sc, O.i, O.v, L_SRC);
    ibar(O.i, L_SRC, L_RO, 0, 1, COL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, nc_pp);
    NCD(0, 0, 0, 0, fw, fh)
    for (int m = 0, w = fw, h = fh; m < cmips; m++, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) NCD(1 + hcur, m, m ? 2 : 1, 0, w, h)
    NCD(3, 0, 3, 1, W / 16, H / 16)
    ibar(O.i, L_RO, L_SRC, 0, 1, COL);
    memset(cb.p, 0, 16);
    csubmit(0, 0); cwait(); read_ts(); frame_done(t);
    const unsigned *c = (const unsigned *)cb.p;
    tot[0] += c[0]; tot[1] += c[1]; tot[2] += c[2]; tot[3] += c[3];
    if (c[0] | c[1] | c[2] | c[3]) { badf++; LOG("  t=%.3f: NaN/Inf channels scene+swarm %u | TAA history %u | bloom/DOF mips %u ; pure-black 16x16 tiles in final %u\n", t, c[0], c[1], c[2], c[3]); }
    if (!(i % 60)) fprintf(stderr, "nancheck frame %d/%d t=%.2f  (%.1f frames/s, %d bad so far)\n", i, nfr, t, i / (now_sec() - c0 + 1e-9), badf);
  }
  LOG("NaN/Inf check: %d of %d frames bad; totals scene+swarm %ld | TAA history %ld | mips %ld | black tiles %ld -> %s (%.1f s)\n", badf, nfr, tot[0], tot[1], tot[2], tot[3], badf ? "FAIL" : "PASS", now_sec() - c0);
  return badf ? 3 : 0;
}
/* two times in different 16 s sections = a deliberate cut */
#define CUTF(a, b) ((int)((a) / 16) != (int)((b) / 16))
/* Flash detector (--lumacheck t0 t1): every frame at 60 fps with continuous TAA history; the GPU
 * sums the luma of the final 8-bit frame inside the letterbox (one uint read back). Two tests:
 *  - spike: a frame differs by more than 6/255 from BOTH neighbours in the same direction (a
 *    one-frame flash; a camera cut is a single step and is not flagged);
 *  - pulse: within 1 s the luma climbs more than 16/255 by smooth steps (cut steps > 8/255 are
 *    ignored) - an exposure/white ramp such as a riser flash. Not tested before t=4.5 s (the
 *    scripted fade-in from black). A one-frame flash is above 6/255; a smooth ramp above 16/255.
 * Writes lumacheck.csv (t, mean luma, 1st and 2nd difference). */
static int dev_lumacheck(float sc) {
  nc_setup();
  int nfr = (int)((luma_t1 - luma_t0) * 60), nsp = 0, y0 = (int)(H * .5f * (1 - (W / 2.2f) / H)) + 2;
  float *L = (float *)malloc(sizeof(float) * (nfr + 1)); double npx = (double)W * (H - 2 * y0);
  LOG("flash check t=%.1f-%.1f s, %d consecutive frames at 60 fps, %dx%d\n", luma_t0, luma_t1, nfr, W, H);
  for (int i = 0; i < nfr; i++) {
    float t = luma_t0 + i / 60.f;
    next_frame(t);
    record_frame(t, sc, O.i, O.v, L_SRC);
    ibar(O.i, L_SRC, L_RO, 0, 1, COL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, nc_pp);
    NCD(3, 0, 4, 2, W, H - 2 * y0)
    ibar(O.i, L_RO, L_SRC, 0, 1, COL);
    memset(nc_cb.p, 0, 32);
    csubmit(0, 0); cwait(); read_ts(); frame_done(t);
    L[i] = (float)(((const unsigned *)nc_cb.p)[4] / npx);
    if (!(i % 60)) fprintf(stderr, "lumacheck frame %d/%d t=%.2f mean luma %.1f\n", i, nfr, t, L[i]);
  }
  FILE *cf = fopen("lumacheck.csv", "w");
  if (cf) fprintf(cf, "t,mean_luma,d1,d2\n");
  float worst = 0, wt = 0;
  int ncut = 0;
  for (int i = 0; i < nfr; i++) {
    float d1 = i ? L[i] - L[i - 1] : 0, d2 = i && i + 1 < nfr ? L[i] - .5f * (L[i - 1] + L[i + 1]) : 0, t = luma_t0 + i / 60.f;
    if (cf) fprintf(cf, "%.4f,%.3f,%.3f,%.3f\n", t, L[i], d1, d2);
    /* section cuts (a neighbour frame in another 16 s section) are deliberate jumps, not flashes */
    if (i && i + 1 < nfr && (CUTF(t - 1 / 60.f, t) || CUTF(t, t + 1 / 60.f))) { ncut++; continue; }
    if (fabsf(d2) > worst) { worst = fabsf(d2); wt = t; }
    if (i && i + 1 < nfr && (L[i] - L[i - 1]) * (L[i] - L[i + 1]) > 0 && fminf(fabsf(L[i] - L[i - 1]), fabsf(L[i] - L[i + 1])) > 6) {
      nsp++; LOG("  spike t=%.3f: mean luma %.1f (prev %.1f, next %.1f)\n", t, L[i], L[i - 1], L[i + 1]); }
  }
  for (int i = 60, last = -999; i < nfr; i++) {
    float r = 0;
    for (int k = i - 59; k <= i; k++) { float d = L[k] - L[k - 1]; if (fabsf(d) < 8) r += d; }
    if (r > 16 && i - last > 60 && luma_t0 + i / 60.f >= 4.5f) { nsp++; last = i; LOG("  pulse t=%.2f: luma rose %.1f/255 within 1 s (now %.1f)\n", luma_t0 + i / 60.f, r, L[i]); }
  }
  if (cf) fclose(cf);
  LOG("flash check: %d spikes; largest |2nd difference| %.2f/255 at t=%.3f (%d section-cut frames skipped) -> %s\n", nsp, worst, wt, ncut, nsp ? "FAIL" : "PASS");
  free(L);
  return nsp ? 4 : 0;
}
/* Real-time playback simulation (--rtcheck T): the release main loop without a window - wall clock
 * from t=0, frames paced to 60 Hz like a FIFO swapchain, the same adapt() supersampling policy and
 * render-target reallocation as the release - so anything that only happens in real playback
 * (resolution switches, TAA history resets) shows up. Per frame: t, scale, render size, GPU ms, mean
 * luma of the final frame, history valid, reallocation -> rtcheck.csv; the frame before and the frame
 * of each of the first reallocations -> rt_*.png. Flags luma spikes like --lumacheck. */
#ifdef _WIN32
static void nap1(void) { Sleep(0); }
#else
static void nap1(void) { struct timespec ts = { 0, 300000 }; nanosleep(&ts, 0); }
#endif
static int dev_rtcheck(float fixed_scale) {
  nc_setup();
  /* CATH_OLDSCALE=1: mid-shot scaler (start 1.5x, +-1/8 step every 24 frames between 11 and 14 ms) */
  int olds = getenv("CATH_OLDSCALE") != 0; float oacc = 0; int onc = 0;
  float scale = fixed_scale > 0 ? fixed_scale : olds ? 1.5f : 1;
  if (fixed_scale <= 0 && !olds) { probe(O.i, O.v); LOG("probe scales:"); for (int k = 0; k < NSEC; k++) LOG(" %.3f", sscale[k]); LOG("\n"); }
  int y0 = (int)(H * .5f * (1 - (W / 2.2f) / H)) + 2, n = 0, nre = 0, nsp = 0, cap = (int)((rt_t1 - rt_t0) * 60) + 600;
  double npx = (double)W * (H - 2 * y0), c0 = now_sec();
  float *L = (float *)malloc(sizeof(float) * cap * 8);
  unsigned char *keep[8]; int kf[8], nk = 0;
  LOG("real-time simulation t=%.1f-%.1f s, %dx%d, 60 Hz pacing, scaler %s\n", rt_t0, rt_t1, W, H, fixed_scale > 0 ? "off" : olds ? "mid-shot adaptive" : "per-section, probed");
  for (; n < cap; n++) {
    double t = rt_t0 + now_sec() - c0; if (t >= rt_t1) break;
    if (fixed_scale <= 0 && !olds) scale = adapt(scale, (float)t);
    int rw = (int)(W * scale), rh = (int)(H * scale), re = 0;
    if (rw != fw || rh != fh) { alloc_target(rw, rh); re = 1; }
    next_frame((float)t);
    record_frame((float)t, scale, O.i, O.v, L_SRC);
    ibar(O.i, L_SRC, L_RO, 0, 1, COL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, nc_pp);
    NCD(3, 0, 4, 2, W, H - 2 * y0)
    ibar(O.i, L_RO, L_SRC, 0, 1, COL);
    { VkBufferImageCopy cp = { 0, 0, 0, { COL, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)W, (uint32_t)H, 1 } }; vkCmdCopyImageToBuffer(cmd, O.i, L_SRC, rb.b, 1, &cp); }
    memset(nc_cb.p, 0, 32);
    float hv = pv[3];
    csubmit(0, 0); cwait(); read_ts(); frame_done((float)t);
    float *r = L + n * 8;
    { /* centre region (middle half of the frame): mean luma and mean |horizontal gradient| (aliasing/noise burst) */
      const unsigned char *q = rb.p; double sl = 0, sg = 0; int cnt = 0;
      for (int y = H / 4; y < H * 3 / 4; y += 6) for (int x = W / 4; x < W * 3 / 4; x += 6) {
        const unsigned char *a = q + ((size_t)y * W + x) * 4; float l0 = .0722f * a[0] + .7152f * a[1] + .2126f * a[2], l1 = .0722f * a[4] + .7152f * a[5] + .2126f * a[6];
        sl += l0; sg += fabsf(l1 - l0); cnt++; }
      r[6] = (float)(sl / cnt); r[7] = (float)(sg / cnt); }
    r[0] = (float)t; r[1] = scale; r[2] = (float)fw; r[3] = gpu_ms[0] + gpu_ms[1] + gpu_ms[2];
    r[4] = (float)(((const unsigned *)nc_cb.p)[4] / npx); r[5] = hv + 2 * re;
    if ((re || (n && L[(n - 1) * 8 + 5] >= 2)) && n > 0 && nk < 8) {   /* the reallocation frame and the one after it */
      keep[nk] = (unsigned char *)malloc((size_t)W * H * 4); memcpy(keep[nk], rb.p, (size_t)W * H * 4); kf[nk++] = n;
    }
    if (re) { nre++; LOG("  t=%.3f: render target reallocated -> scale %.3f (%dx%d), TAA history reset\n", t, scale, fw, fh); }
    if (fixed_scale <= 0 && olds) {
      oacc += gpu_ms[0] + gpu_ms[1]; onc++;
      if (onc >= 24) { float ms = oacc / onc, up = (scale + .125f) / scale;
        if (ms > 14 && scale > .5f) scale -= .125f; else if (ms * up * up < 11 && scale < 2) scale += .125f; onc = 0; oacc = 0; }
    }
    double nx = c0 + (n + 1) / 60.0; while (now_sec() < nx) nap1();
  }
  FILE *cf = fopen("rtcheck.csv", "w");
  if (cf) fprintf(cf, "t,scale,render_w,gpu_ms,mean_luma,flags,centre_luma,centre_grad\n");
  float worst = 0, wt = 0; int ncut = 0;
  for (int i = 0; i < n; i++) {
    float *r = L + i * 8;
    if (cf) fprintf(cf, "%.4f,%.3f,%.0f,%.2f,%.3f,%.0f,%.3f,%.3f\n", r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    if (i && i + 1 < n && (CUTF(r[-8], r[0]) || CUTF(r[0], r[8]))) { ncut++; continue; }   /* skip section-cut frames */
    if (i && i + 1 < n) {
      float a = r[4] - r[-4], b = r[4] - r[12], d2 = fabsf(r[4] - .5f * (r[-4] + r[12]));
      float ca = r[6] - r[-2], cb = r[6] - r[14];
      if (ca * cb > 0 && fminf(fabsf(ca), fabsf(cb)) > 3) { nsp++; LOG("  centre spike t=%.3f: centre luma %.1f (prev %.1f, next %.1f)\n", r[0], r[6], r[-2], r[14]); }
      if (d2 > worst) { worst = d2; wt = r[0]; }
      if (a * b > 0 && fminf(fabsf(a), fabsf(b)) > 3) { nsp++; LOG("  spike t=%.3f: mean luma %.1f (prev %.1f, next %.1f)%s\n", r[0], r[4], r[-4], r[12], r[5] >= 2 ? " [realloc]" : r[5] < .5f ? " [history reset]" : ""); }
    }
  }
  if (cf) fclose(cf);
  unsigned char *rgb = (unsigned char *)malloc((size_t)W * H * 3);
  for (int k = 0; k < nk; k++) {
    for (int i = 0; i < W * H; i++) { rgb[i * 3] = keep[k][i * 4 + 2]; rgb[i * 3 + 1] = keep[k][i * 4 + 1]; rgb[i * 3 + 2] = keep[k][i * 4]; }
    char fn[64]; snprintf(fn, sizeof fn, "rt_f%04d_t%07.3f.png", kf[k], L[kf[k] * 8]); write_png(fn, rgb, W, H);
  }
  LOG("real-time simulation: %d frames, %d render-target reallocations, %d luma spikes (>3/255 both sides); largest |2nd difference| %.2f/255 at t=%.3f (%d section-cut frames skipped) -> %s\n",
      n, nre, nsp, worst, wt, ncut, nsp ? "FAIL" : "PASS");
  return nsp ? 5 : 0;
}
static int dev_selftest(float fixed_scale) {
#ifdef _WIN32
  if (raw_fps > 0) _setmode(_fileno(stdout), _O_BINARY);
#endif
  lg = raw_fps > 0 ? 0 : fopen("cathedra_selftest.log", "w");
  uint32_t av = gprops.apiVersion; const int *q = tiers[tier];
  LOG("CATHEDRA 1.0 self-test (dev build)\ndevice: %s (type %d, vendor 0x%04x, driver 0x%08x)\nVulkan API: %u.%u.%u\n", gprops.deviceName,
      (int)gprops.deviceType, gprops.vendorID, gprops.driverVersion, av >> 22, (av >> 12) & 1023, av & 4095);
  LOG("extensions: VK_KHR_acceleration_structure=1 VK_KHR_ray_query=1 VK_KHR_ray_tracing_pipeline=%d VK_EXT_mesh_shader=%d\n", has_rtp, has_mesh);
  double mb = (q[0] * (double)q[1] * q[2] * 8 + (double)q[3] * q[3] * q[3] * 8 + q[4] * (double)q[4] / 2 * 8 * 4 / 3 + (double)npanels * 80
               + (double)ntri * 72 + 128. * 128 * 128 * 4 * 8 / 7 + q[0] / 4. * (q[1] / 2 + 8) * q[2] / 4 * 8) / 1048576.;
  LOG("quality tier %d: clouds %dx%dx%d RGBA16F, planet volume %d^3 RGBA16F, planet map %dx%d RGBA16F (mipmapped), city light %dx%dx%d RGBA16F\n", tier, q[0], q[1], q[2], q[3], q[4], q[4] / 2, q[0] / 4, q[1] / 2 + 8, q[2] / 4);
  LOG("architecture: %d tower variants, %u parts -> %u triangles; %d tower instances; swarm %d elements, %d debris chunks\n",
      q[5], npart, ntri, ntow, npanels, nch);
  LOG("precalc memory (volumes, maps, swarm, meshes; BVHs extra): %.0f MB\n", mb);
  LOG("precalc (wall clock; every GPU stage ends in a fence wait, so these include GPU completion): pipelines %.2f s | swarm+noise+meshes %.2f s | planet map %.2f s | planet volume %.2f s | BVHs %.2f s | clouds (incl. ray-traced tower shadows) %.2f s | city light volume (%d rays/voxel + shadow rays) %.2f s | music ready %.2f s | total %.2f s\n",
      tst[1] - tst[0], tst[2] - tst[1], tst[3] - tst[2], tst[4] - tst[3], tst[5] - tst[4], tst[6] - tst[5], (32 << tier) * (tier > 2 ? 8 : 1), tst[7] - tst[6], tst[8] - tst[0], tst[8] - tst[0]);
  float sc = fixed_scale > 0 ? fixed_scale : 1;
  alloc_target((int)(W * sc), (int)(H * sc));
  O = mkimg(0, VK_FORMAT_B8G8R8A8_UNORM, W, H, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  rb = mkbuf((VkDeviceSize)W * H * 4, 1);
  unsigned char *rgb = (unsigned char *)malloc((size_t)W * H * 3);
  int nfr = raw_fps > 0 ? (int)((raw_t1 - raw_t0) * raw_fps) : 0;
  if (!nfr && frames > 0) {
    /* per section: <frames> timed frames spread evenly over the whole 16 s (each after 2 warm-up
       frames 1/60 s earlier, so TAA history and BVH refits are realistic); avg, p95 and worst of
       the GPU total, plus the average split */
    LOG("render %dx%d, scene scale %.2f, %d frames per section spread over the section (each after 2 warm-up frames)\n", W, H, sc, frames);
    float *tt = (float *)malloc(sizeof(float) * frames);
    for (int s = 0; s < NSEC; s++) {
      float acc[3] = { 0, 0, 0 }, wall = 0, worst = 0, wt = 0;
      for (int f = 0; f < frames; f++) {
        float t = s * 16 + (f + .5f) * 16.f / frames;
        dev_frame(t - 2 / 60.f, sc, 0); dev_frame(t - 1 / 60.f, sc, 0);
        double c0 = now_sec();
        dev_frame(t, sc, 0);
        wall += (float)(now_sec() - c0) * 1000;
        for (int i = 0; i < 3; i++) acc[i] += gpu_ms[i];
        tt[f] = gpu_ms[0] + gpu_ms[1] + gpu_ms[2];
        if (tt[f] > worst) { worst = tt[f]; wt = t; }
      }
      for (int i = 1; i < frames; i++) for (int j = i; j > 0 && tt[j - 1] > tt[j]; j--) { float x = tt[j]; tt[j] = tt[j - 1]; tt[j - 1] = x; }
      float n = (float)frames, p95 = tt[(int)(.95f * (frames - 1) + .5f)];
      LOG("section %2d (t=%5.1f-%5.1f): GPU total avg %7.2f  p95 %7.2f  worst %7.2f ms (t=%5.1f) | BVH %5.2f  scene %7.2f  TAA+post %5.2f | wall %7.1f ms/frame\n",
          s, s * 16.f, s * 16 + 16.f, (acc[0] + acc[1] + acc[2]) / n, p95, worst, wt, acc[0] / n, acc[1] / n, acc[2] / n, wall / n);
    }
    free(tt);
  }
  if (nan_t0 >= 0) { int r = dev_nancheck(sc); if (r) { LOG("self-test FAILED (NaN/Inf)\n"); return r; } }
  if (rt_t1 > 0) return dev_rtcheck(fixed_scale);
  if (luma_t0 >= 0) { int r = dev_lumacheck(sc); if (r) { LOG("self-test FAILED (flashes)\n"); return r; } }
  if (sweep) {
    LOG("per-second sweep (3 warm frames, then 1 timed frame per second):\n");
    float worst = 0, wt = 0, st[16]; int ns = 0;
    for (int s = 0; s < (int)SONG_SEC; s++) {
      for (int f = 0; f < 4; f++) dev_frame(s + .5f + f / 60.f, sc, 0);
      float tot = gpu_ms[0] + gpu_ms[1] + gpu_ms[2];
      if (tot > worst) { worst = tot; wt = s + .5f; }
      LOG("sweep t=%5.1f  BVH %6.2f  scene %7.2f  TAA+post %5.2f  total %7.2f ms\n", s + .5f, gpu_ms[0], gpu_ms[1], gpu_ms[2], tot);
      st[ns++] = tot;
      if (ns == 16 || s + 1 == (int)SONG_SEC) {   /* per-section summary of the sweep */
        float sm = 0, mx = 0;
        for (int i = 1; i < ns; i++) for (int j = i; j > 0 && st[j - 1] > st[j]; j--) { float x = st[j]; st[j] = st[j - 1]; st[j - 1] = x; }
        for (int i = 0; i < ns; i++) { sm += st[i]; mx = st[i] > mx ? st[i] : mx; }
        LOG("sweep section %2d: avg %7.2f  p95 %7.2f  worst %7.2f ms\n", s / 16, sm / ns, st[(int)(.95f * (ns - 1) + .5f)], mx);
        ns = 0;
      }
    }
    LOG("sweep worst: %.2f ms at t=%.1f\n", worst, wt);
  }
  for (int i = 0; i < (nfr ? nfr : nshots); i++) {
    float t = nfr ? raw_t0 + (still ? 0 : i / raw_fps) : shots[i];
    /* warm the GPU before a shot: >= 0.4 s of frames, then the median of the last 5 (4 lead-in + the shot) */
    float hist[5][3]; int nh = 0;
    if (!nfr) {
      double w0 = now_sec(); while (now_sec() - w0 < .4) dev_frame(t - (taa_lead + 1) / 60.f, sc, 0);
      for (int k = taa_lead; k > 0; k--) { dev_frame(t - k / 60.f, sc, 0); if (k <= 4) { memcpy(hist[nh], gpu_ms, sizeof gpu_ms); nh++; } }
    }
    double c0 = now_sec();
    dev_frame(t, sc, 1);
    memcpy(hist[nh], gpu_ms, sizeof gpu_ms); nh++;
    float ms[3];
    for (int c = 0; c < 3; c++) {   /* per-pass median */
      float v[5]; for (int j = 0; j < nh; j++) { v[j] = hist[j][c]; for (int q = j; q > 0 && v[q - 1] > v[q]; q--) { float x = v[q]; v[q] = v[q - 1]; v[q - 1] = x; } }
      ms[c] = v[nh / 2];
    }
    for (int p = 0; p < W * H; p++) { rgb[p * 3] = rb.p[p * 4 + 2]; rgb[p * 3 + 1] = rb.p[p * 4 + 1]; rgb[p * 3 + 2] = rb.p[p * 4]; }
    if (nfr) { fwrite(rgb, 1, (size_t)W * H * 3, stdout); if (!(i % 30)) fprintf(stderr, "frame %d/%d t=%.2f\n", i, nfr, t); continue; }
    char fn[64]; snprintf(fn, sizeof fn, "shot_t%05.1f.png", t);
    write_png(fn, rgb, W, H);
    LOG("%s  GPU BVH %.2f ms  scene %.2f ms  TAA+post %.2f ms  total %.2f ms  (median of %d warm frames; wall %.0f ms incl. PNG)\n", fn, ms[0], ms[1], ms[2], ms[0] + ms[1] + ms[2], nh, (now_sec() - c0) * 1000);
  }
  LOG("self-test OK\n");
  return 0;
}

/* ===================== YouTube / offline export ==========================
 * --export <dir|-> [--size 3840x2160] [--fps 60|30] [--from s] [--to s] [--section n]
 *   [--first N] [--last N] [--redo] [--nowav] [--bits 16|8] [--spp n] [-s scale] [--wav 32|24]
 * Writes <dir>/cathedra_48k.wav (offline synth master resampled to 48 kHz,
 * 32-bit float or 24-bit PCM; not with --nowav), <dir>/frame_NNNNNN.png (frame N = time N/fps,
 * 16-bit RGB by default) and <dir>/export_info.txt. Frame numbers are the whole film's, so a
 * range (--section, --from/--to in seconds, --first/--last in frames) drops into a full export.
 * Existing frames are skipped (resume-safe; frames are written to .tmp and renamed) unless
 * --redo. '-' instead of a dir streams raw rgb48le/rgb24 frames to stdout for an ffmpeg pipe. */


/* --- a small, fast zlib/deflate encoder (fixed Huffman + greedy LZ77) so the
 * 4K sequence is compressed without an external library --- */
typedef struct { unsigned char *o; unsigned long long acc; int n; } BW;
static unsigned short fh_code[288]; static unsigned char fh_len[288], lc_tab[259], dc_tab[32769];
static const unsigned short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static unsigned rev_(unsigned v, int n) { unsigned r = 0; while (n--) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
static void zinit(void) {
  for (int s = 0; s < 288; s++) {
    int l = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
    unsigned c = s < 144 ? 0x30 + s : s < 256 ? 0x190 + s - 144 : s < 280 ? s - 256 : 0xC0 + s - 280;
    fh_code[s] = (unsigned short)rev_(c, l); fh_len[s] = (unsigned char)l;
  }
  for (int i = 0, c = 0; i <= 258; i++) { while (c < 28 && lbase[c + 1] <= i) c++; lc_tab[i] = (unsigned char)c; }
  for (int i = 1, c = 0; i <= 32768; i++) { while (c < 29 && dbase[c + 1] <= i) c++; dc_tab[i] = (unsigned char)c; }
}
static inline void put(BW *b, unsigned v, int n) {
  b->acc |= (unsigned long long)v << b->n; b->n += n;
  while (b->n >= 8) { *b->o++ = (unsigned char)b->acc; b->acc >>= 8; b->n -= 8; }
}
static size_t zcompress(const unsigned char *d, size_t n, unsigned char *out) {
  BW b = { out, 0, 0 };
  int *head = (int *)malloc(sizeof(int) << 16);
  memset(head, 0xff, sizeof(int) << 16);
  put(&b, 0x78, 8); put(&b, 0x01, 8); put(&b, 1, 1); put(&b, 1, 2);
  size_t i = 0;
  while (i < n) {
    size_t best = 0, dist = 0;
    if (i + 3 <= n) {
      unsigned h = ((d[i] << 16 | d[i + 1] << 8 | d[i + 2]) * 2654435761u) >> 16;
      int c = head[h]; head[h] = (int)i;
      if (c >= 0 && i - c <= 32768) {
        size_t m = 0, mx = n - i < 258 ? n - i : 258;
        while (m < mx && d[c + m] == d[i + m]) m++;
        if (m >= 3) { best = m; dist = i - c; }
      }
    }
    if (best) {
      int lc = lc_tab[best], dc = dc_tab[dist];
      put(&b, fh_code[257 + lc], fh_len[257 + lc]); if (lext[lc]) put(&b, (unsigned)(best - lbase[lc]), lext[lc]);
      put(&b, rev_(dc, 5), 5); if (dext[dc]) put(&b, (unsigned)(dist - dbase[dc]), dext[dc]);
      i += best;
    } else { put(&b, fh_code[d[i]], fh_len[d[i]]); i++; }
  }
  put(&b, fh_code[256], fh_len[256]);
  if (b.n) put(&b, 0, 8 - b.n);
  unsigned a = 1, s2 = 0;
  for (size_t k = 0; k < n; ) { size_t e = k + 5552 < n ? k + 5552 : n; for (; k < e; k++) { a += d[k]; s2 += a; } a %= 65521; s2 %= 65521; }
  unsigned ad = s2 << 16 | a;
  for (int k = 3; k >= 0; k--) *b.o++ = (unsigned char)(ad >> (8 * k));
  free(head);
  return (size_t)(b.o - out);
}
/* PNG with Paeth-filtered rows, RGB 8 or 16 bit; src = GPU readback (BGRA8 or RGBA16 UNORM) */
static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
static void write_png2(const char *fn, const unsigned char *src, int w, int h, int bits) {
  int bpp = bits == 16 ? 6 : 3; size_t rl = (size_t)w * bpp, n = (rl + 1) * h;
  unsigned char *raw = (unsigned char *)malloc(rl * 2), *f = (unsigned char *)malloc(n), *z = (unsigned char *)malloc(n + n / 4 + 1024), ih[13];
  unsigned char *prev = raw, *cur = raw + rl; memset(prev, 0, rl);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (bits == 16) { const unsigned short *s = (const unsigned short *)(src + ((size_t)y * w + x) * 8);
        for (int c = 0; c < 3; c++) { cur[x * 6 + c * 2] = (unsigned char)(s[c] >> 8); cur[x * 6 + c * 2 + 1] = (unsigned char)s[c]; } }
      else { const unsigned char *s = src + ((size_t)y * w + x) * 4; cur[x * 3] = s[2]; cur[x * 3 + 1] = s[1]; cur[x * 3 + 2] = s[0]; }
    }
    unsigned char *o = f + (rl + 1) * y; *o++ = 4;
    for (size_t i = 0; i < rl; i++) o[i] = (unsigned char)(cur[i] - paeth(i >= (size_t)bpp ? cur[i - bpp] : 0, prev[i], i >= (size_t)bpp ? prev[i - bpp] : 0));
    unsigned char *t = prev; prev = cur; cur = t;
  }
  size_t zn = zcompress(f, n, z);
  FILE *fp = fopen(fn, "wb");
  if (fp) {
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, fp);
    be32(ih, w); be32(ih + 4, h); ih[8] = (unsigned char)bits; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(fp, "IHDR", ih, 13); chunk(fp, "IDAT", z, (unsigned)zn); chunk(fp, "IEND", 0, 0);
    fclose(fp);
  }
  free(raw); free(f); free(z);
}
/* CPU encoder pool: one slot per worker; the render thread only waits when every slot is busy */
#define NSLOT 32
typedef struct { unsigned char *px; char fn[600], tmp[604]; int busy; thr_t th; } Slot;   /* tmp = fn + ".tmp" */
static Slot slots[NSLOT]; static int nslot, ex_w, ex_h;
static THRET enc_job(void *a) {
  Slot *s = (Slot *)a;
  write_png2(s->tmp, s->px, ex_w, ex_h, exp_bits);
  remove(s->fn); rename(s->tmp, s->fn);
  return 0;
}
static int file_ok(const char *fn) { FILE *f = fopen(fn, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n > 64; }
/* float master (SR) -> 48 kHz, windowed-sinc (64 taps, Blackman-Harris; at SR = 48 kHz a plain copy, give or take a filter
 * edge at 23 kHz), 32-bit float or 24-bit WAV */
static void write_wav(const char *fn) {
  const int OR = 48000; size_t nout = (size_t)SONG_SEC * OR;
  FILE *f = fopen(fn, "wb"); if (!f) return;
  int bps = exp_wav == 24 ? 3 : 4; unsigned dsz = (unsigned)(nout * 2 * bps);
  unsigned char hd[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0";
  unsigned v[] = { 36 + dsz };
  memcpy(hd + 4, v, 4);
  unsigned short fmt = exp_wav == 24 ? 1 : 3, ch = 2, ba = (unsigned short)(2 * bps), bits = (unsigned short)(8 * bps); unsigned sr = OR, br = OR * 2 * bps;
  memcpy(hd + 20, &fmt, 2); memcpy(hd + 22, &ch, 2); memcpy(hd + 24, &sr, 4); memcpy(hd + 28, &br, 4); memcpy(hd + 32, &ba, 2); memcpy(hd + 34, &bits, 2);
  memcpy(hd + 36, "data", 4); memcpy(hd + 40, &dsz, 4);
  fwrite(hd, 1, 44, f);
  const double ratio = (double)SR / OR, fc = 0.97 * 0.5;   /* upsampling: cut just below the source Nyquist */
  unsigned char *buf = (unsigned char *)malloc((size_t)OR * 2 * bps);
  for (size_t o0 = 0; o0 < nout; o0 += OR) {
    size_t cnt = nout - o0 < (size_t)OR ? nout - o0 : (size_t)OR;
    for (size_t j = 0; j < cnt; j++) {
      double x = (o0 + j) * ratio; long k0 = (long)floor(x);
      double acc[2] = { 0, 0 }, ws = 0;
      for (long k = k0 - 31; k <= k0 + 32; k++) {
        double u = x - k, a = (u + 32) / 64.0, sn = fabs(u) < 1e-9 ? 2 * fc : sin(2 * M_PI * fc * u) / (M_PI * u);
        double wv = a <= 0 || a >= 1 ? 0 : .35875 - .48829 * cos(2 * M_PI * a) + .14128 * cos(4 * M_PI * a) - .01168 * cos(6 * M_PI * a);
        double g = sn * wv; ws += g;
        if (k >= 0 && k < NFR) { acc[0] += pcmf[2 * k] * g; acc[1] += pcmf[2 * k + 1] * g; }
      }
      for (int c = 0; c < 2; c++) {
        double s = acc[c] / ws; unsigned char *p = buf + (j * 2 + c) * bps;
        if (bps == 4) { float fv = (float)s; memcpy(p, &fv, 4); }
        else { long q = lround(fmax(-1., fmin(1., s)) * 8388607.); p[0] = (unsigned char)q; p[1] = (unsigned char)(q >> 8); p[2] = (unsigned char)(q >> 16); }
      }
    }
    fwrite(buf, 1, cnt * 2 * bps, f);
  }
  free(buf); fclose(f);
}
static int dev_export(float scale) {
  int pipe_ = !strcmp(exp_dir, "-");
  char fn[600];
  int f0 = (int)(exp_from * exp_fps + .5f), f1 = (int)(exp_to * exp_fps + .5f), nf;
  if (exp_f0 >= 0 || exp_f1 >= 0) {   /* --first/--last: an exact frame range, clamped to the film */
    int fe = (int)(SONG_SEC * exp_fps);
    f0 = exp_f0 >= 0 ? exp_f0 : 0; f1 = exp_f1 >= 0 ? exp_f1 + 1 : fe;
    if (f1 > fe) f1 = fe;
    if (f0 > f1 - 1) f0 = f1 - 1;
  }
  nf = f1 - f0;
  if (scale <= 0) scale = 1;
  ex_w = W; ex_h = H;
  if (!pipe_) {
#ifdef _WIN32
    _mkdir(exp_dir);
#else
    mkdir(exp_dir, 0755);
#endif
    snprintf(fn, sizeof fn, "%s/export_info.txt", exp_dir);
    FILE *f = fopen(fn, "w");
    if (!f) { fprintf(stderr, "cannot write to %s\n", exp_dir); return 1; }
    fprintf(f, "CATHEDRA 1.0 export\nresolution: %dx%d\nfps: %d\nframes: %d (frame_%06d.png .. frame_%06d.png)\nfirst frame time: %.6f s\n"
               "audio: %s, 48000 Hz stereo, %s, %d s, audio offset 0 (frame N starts at N/%d s)\n"
               "pixel format: RGB %d-bit PNG, sRGB (Rec.709 primaries), full range\nrender: %d samples per frame (jittered, accumulated), internal scale %.2f, quality tier %d\n",
            W, H, exp_fps, nf, f0, f1 - 1, f0 / (double)exp_fps, exp_nowav ? "not written (--nowav)" : "cathedra_48k.wav", exp_wav == 24 ? "24-bit PCM" : "32-bit float", (int)SONG_SEC, exp_fps, exp_bits, exp_spp, scale, tier);
    fclose(f);
    snprintf(fn, sizeof fn, "%s/cathedra_48k.wav", exp_dir);
    if (!exp_nowav) { fprintf(stderr, "writing %s ...\n", fn); write_wav(fn); }
  } else {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (!exp_nowav) write_wav("cathedra_48k.wav");
    fprintf(stderr, "%s; piping raw %s %dx%d @ %d fps to stdout, e.g.\n  cathedra_dev --export - | ffmpeg -f rawvideo -pix_fmt %s -s %dx%d -r %d -i - -i cathedra_48k.wav -c:v libx265 -crf 12 -pix_fmt yuv420p10le -c:a aac -b:a 384k out.mp4\n",
                  exp_nowav ? "no WAV (--nowav)" : "wrote cathedra_48k.wav", exp_bits == 16 ? "rgb48le" : "rgb24", W, H, exp_fps, exp_bits == 16 ? "rgb48le" : "rgb24", W, H, exp_fps);
  }
  VkFormat of = exp_bits == 16 ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
  pip_post = mkgfx(SM(spv_quad), SM(spv_post), of, 0);
  alloc_target((int)(W * scale), (int)(H * scale));
  O = mkimg(0, of, W, H, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  size_t fb = (size_t)W * H * (exp_bits == 16 ? 8 : 4);
  rb = mkbuf(fb, 1);
  zinit();
#ifdef _WIN32
  SYSTEM_INFO si; GetSystemInfo(&si); nslot = (int)si.dwNumberOfProcessors - 1;
#else
  nslot = (int)sysconf(_SC_NPROCESSORS_ONLN) - 1;
#endif
  if (nslot < 1) nslot = 1;
  if (nslot > NSLOT) nslot = NSLOT;
  for (int i = 0; i < nslot; i++) slots[i].px = (unsigned char *)malloc(fb);
  unsigned char *line = pipe_ ? (unsigned char *)malloc((size_t)W * 6) : 0;
  double c0 = now_sec(); int done = 0, skipped = 0;
  for (int fi = f0; fi < f1; fi++) {
    if (!pipe_) { snprintf(fn, sizeof fn, "%s/frame_%06d.png", exp_dir, fi); if (!exp_redo && file_ok(fn)) { skipped++; continue; } }
    float t = fi / (float)exp_fps;
    /* exp_spp jittered renders of the same instant, averaged by the TAA pass (accumulation mode) */
    for (int k = 0; k < exp_spp; k++) {
      int n = k + 1; float hx = 0, hy = 0, f = .5f;
      for (int i = n; i; i >>= 1, f *= .5f) hx += f * (i & 1);
      f = 1.f / 3; for (int i = n; i; i /= 3, f /= 3) hy += f * (i % 3);
      pv[0] = t; pv[1] = hx - .5f; pv[2] = hy - .5f; pv[3] = k ? (float)(k + 1) : 0.f;
      record_frame(t, scale, O.i, O.v, L_SRC);
      if (k == exp_spp - 1) { VkBufferImageCopy cp = { 0, 0, 0, { COL, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)W, (uint32_t)H, 1 } }; vkCmdCopyImageToBuffer(cmd, O.i, L_SRC, rb.b, 1, &cp); }
      csubmit(0, 0); cwait();
    }
    if (pipe_) {
      for (int y = 0; y < H; y++) {
        if (exp_bits == 16) { const unsigned short *s = (const unsigned short *)(rb.p + (size_t)y * W * 8); unsigned short *o = (unsigned short *)line;
          for (int x = 0; x < W; x++) { o[x * 3] = s[x * 4]; o[x * 3 + 1] = s[x * 4 + 1]; o[x * 3 + 2] = s[x * 4 + 2]; } fwrite(line, 6, W, stdout); }
        else { const unsigned char *s = rb.p + (size_t)y * W * 4;
          for (int x = 0; x < W; x++) { line[x * 3] = s[x * 4 + 2]; line[x * 3 + 1] = s[x * 4 + 1]; line[x * 3 + 2] = s[x * 4]; } fwrite(line, 3, W, stdout); }
      }
    } else {
      Slot *s = &slots[done % nslot];
      if (s->busy) thr_join(s->th);
      memcpy(s->px, rb.p, fb);
      snprintf(s->fn, sizeof s->fn, "%s", fn); snprintf(s->tmp, sizeof s->tmp, "%s.tmp", fn);
      s->busy = 1; s->th = thr_go(enc_job, s);
    }
    done++;
    if (done % 30 == 0 || fi == f1 - 1) {
      double el = now_sec() - c0, rate = done / el;
      fprintf(stderr, "frame %d/%d (t=%.2f s)  %.2f fps  ETA %.0f min\n", fi - f0 + 1, nf, t, rate, (nf - skipped - done) / rate / 60);
    }
  }
  for (int i = 0; i < nslot; i++) if (slots[i].busy) thr_join(slots[i].th);
  fprintf(stderr, "export done: %d frames rendered, %d already present (skipped), %.1f min\n", done, skipped, (now_sec() - c0) / 60);
  return 0;
}
