/* CATHEDRA softsynth - renders the whole 160 s soundtrack procedurally.
 * No samples: every sound is oscillators, noise and filters driven by the
 * pattern tables below. Also records per-instrument envelopes at 100 Hz that
 * the shaders use for audio-reactive sync. */
#define SR       48000
#define SONG_SEC 176
#define NFR      (SR*SONG_SEC)
#define SYNC_HZ  100
#define NSYNC    (SONG_SEC*SYNC_HZ)
#define TAU      6.2831853f

#define WIN      (SR*20)       /* per-section render window (16 s + tails) */
#if defined(_MSC_VER)
#define TLS __declspec(thread)
#else
#define TLS __thread
#endif
#ifdef _WIN32
#define __sync_fetch_and_add(p, v) InterlockedExchangeAdd((volatile LONG *)(p), (v))
#define THRET DWORD WINAPI
typedef HANDLE thr_t;
static thr_t thr_go(LPTHREAD_START_ROUTINE f, void *a) { return CreateThread(0, 0, f, a, 0, 0); }
static void thr_join(thr_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
#define THRET void *
typedef pthread_t thr_t;
static thr_t thr_go(void *(*f)(void *), void *a) { pthread_t t; pthread_create(&t, 0, f, a); return t; }
static void thr_join(thr_t t) { pthread_join(t, 0); }
#endif

/* Each song section renders on its own thread into a private window; these
 * thread-local pointers are pre-offset so instruments can use song positions.
 * Windows: mingw's __thread is emulated (a call per access, ~1.3 KB packed), so
 * the per-thread block lives on the section thread's stack and its address in
 * the TEB's NT_TIB.ArbitraryUserPointer slot (gs:0x28). */
#ifdef _WIN32
#include <intrin.h>
struct tctx { float *busL, *busR, *drmL, *drmR, *rvS, *dlS; float (*syn)[NSYNC]; int LIM; unsigned rngs; };
#define TC ((struct tctx *)__readgsqword(0x28))
#define busL (TC->busL)
#define busR (TC->busR)
#define drmL (TC->drmL)
#define drmR (TC->drmR)
#define rvS (TC->rvS)
#define dlS (TC->dlS)
#define syn (TC->syn)
#define LIM (TC->LIM)
#define rngs (TC->rngs)
#else
static TLS float *busL, *busR;     /* ducked music bus (pads, bass, arp, lead) */
static TLS float *drmL, *drmR;     /* drums + fx bus */
static TLS float *rvS, *dlS;       /* mono reverb / delay sends */
static TLS float (*syn)[NSYNC];     /* sync envelopes */
static TLS int LIM;                /* end of this thread's window */
static TLS unsigned rngs;
#endif
static float *gbus[6];             /* merged song-length buses */
static short *pcm;                 /* final interleaved 16-bit stereo */
#ifdef CATHEDRA_DEV
static float *pcmf;                /* dev: float master (same samples before 16-bit quantisation) */
#endif
static float (*syncv)[NSYNC];      /* [8][NSYNC] kick snare hat arp bass lead impact riser */

#define NSEC 11
enum { KICK=1, SNARE=2, HAT=4, BASS=8, PAD=16, ARP=32, LEAD=64, SUB=128,
       RISE=256, IMPACT=512, OHAT=1024, CHOIR=2048, BELL=4096 };
static const unsigned short arrange[NSEC] = {
  PAD|SUB|CHOIR|BELL,                             /* 0 dawn            */
  PAD|ARP|SUB|RISE|CHOIR,                         /* 1 reveal          */
  PAD|ARP|KICK|BASS|HAT|IMPACT,                   /* 2 flight          */
  PAD|ARP|KICK|BASS|HAT|SNARE|OHAT|BELL,          /* 3 nave            */
  PAD|ARP|SUB|RISE|CHOIR,                         /* 4 breakdown       */
  PAD|ARP|KICK|BASS|HAT|SNARE|OHAT|LEAD|IMPACT,   /* 5 core / drop     */
  PAD|ARP|KICK|BASS|HAT|SNARE|OHAT|LEAD|RISE,     /* 6 ascent          */
  PAD|ARP|KICK|BASS|HAT|IMPACT,                   /* 7 orbit           */
  PAD|ARP|SUB|CHOIR|RISE,                         /* 8 eclipse         */
  PAD|ARP|KICK|BASS|HAT|SNARE|OHAT|LEAD|IMPACT|CHOIR, /* 9 the star   */
  PAD|IMPACT|CHOIR|BELL                           /* 10 outro          */
};
/* i - VI - III - VII  (Am F C G), 2 bars each */
static const unsigned char chords[4][5] = {
  {45, 57,60,64,71}, {41, 57,60,64,65}, {36, 55,60,64,67}, {43, 55,59,62,67}
};
/* lead melody: start(16th), length(16th), midi note  - 8 bars */
static const unsigned char melody[][3] = {
  {0,6,76},{6,2,74},{8,4,72},{12,4,74},{16,8,76},{24,4,79},{28,4,76},
  {32,6,77},{38,2,76},{40,4,72},{44,4,69},{48,12,72},{60,4,74},
  {64,6,76},{70,2,74},{72,4,72},{76,4,79},{80,8,84},{88,4,83},{92,4,79},
  {96,8,81},{104,4,79},{108,4,74},{112,12,76},{124,4,74}
};
static const unsigned char arpPat[16] = {0,2,1,3, 2,4,3,5, 4,6,5,7, 6,4,3,1};

static float frnd(void) { rngs = rngs*1664525u + 1013904223u; return (float)(rngs >> 9) * (2.0f/8388608.0f) - 1.0f; }
static float mtof(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
static int  secs(float t) { return (int)(t * SR); }

static void addsync(int ch, int s0, float decay, float amp) {
  int k0 = s0 / (SR/SYNC_HZ), n = (int)(SYNC_HZ * 6.0f / decay) + 1;
  for (int k = k0; k < k0 + n && k < NSYNC; k++) {
    float v = amp * expf(-((float)k / SYNC_HZ - (float)s0 / SR) * decay);
    if (k >= 0 && v > syn[ch][k]) syn[ch][k] = v;
  }
}
/* PolyBLEP band-limited saw, phase in [0,1) */
static float saw(float ph, float dt) {
  float v = 2.0f*ph - 1.0f;
  if (ph < dt)        { float x = ph/dt;          v -= x+x - x*x - 1.0f; }
  else if (ph > 1-dt) { float x = (ph-1.0f)/dt;   v -= x*x + x+x + 1.0f; }
  return v;
}
typedef struct { float lp, bp; } SVF;
static float fco(float fc) { return 2.0f * sinf(3.14159265f * fminf(fc, SR*0.16f) / SR); }
static float svf(SVF *s, float in, float f, float q) {   /* returns lowpass; s->bp holds band */
  s->lp += f * s->bp;
  float hp = in - s->lp - q * s->bp;
  s->bp += f * hp;
  return s->lp;
}

static void kick(int s0, float amp) {
  float ph = 0;
  for (int i = 0; i < SR/2 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    ph += (44.0f + 150.0f*expf(-t*30.0f) + 400.0f*expf(-t*250.0f)) / SR;
    float v = sinf(ph*TAU) * expf(-t*5.0f) + frnd()*expf(-t*350.0f)*0.25f;
    v = tanhf(v * 1.8f) * amp;
    drmL[s0+i] += v; drmR[s0+i] += v;
  }
  addsync(0, s0, 7.0f, 1.0f);
}
static void snare(int s0, float amp) {
  SVF f = {0,0}; float ph = 0;
  for (int i = 0; i < SR*4/10 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    ph += (175.0f + 60.0f*expf(-t*40.0f)) / SR;
    svf(&f, frnd(), 0.36f, 0.7f);
    float v = (f.bp*1.3f*expf(-t*13.0f) + sinf(ph*TAU)*0.6f*expf(-t*25.0f)) * amp;
    drmL[s0+i] += v; drmR[s0+i] += v*0.95f; rvS[s0+i] += v*0.5f;
  }
  addsync(1, s0, 9.0f, amp*1.5f);
}
static void hat(int s0, float amp, float dec, float pan) {
  float lp = 0;
  for (int i = 0; i < (int)(SR*6.0f/dec) && s0+i < LIM; i++) {
    float t = (float)i / SR, n = frnd();
    lp += (n - lp) * 0.52f;
    float v = (n - lp) * expf(-t*dec) * amp;
    drmL[s0+i] += v*(1.0f-pan); drmR[s0+i] += v*(1.0f+pan); dlS[s0+i] += v*0.15f;
  }
  addsync(2, s0, dec*0.5f, amp*3.0f);
}
static void bass(int s0, int len, float note, float amp) {
  SVF f = {0,0}; float p1 = 0, p2 = 0, p3 = 0, fr = mtof(note), fc = 0;
  for (int i = 0; i < len + SR/20 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    float env = (i < len ? 1.0f : expf(-(float)(i-len)/SR*60.0f)) * fminf(1.0f, t*300.0f);
    p1 += fr*1.003f/SR; p1 -= (int)p1; p2 += fr*0.997f/SR; p2 -= (int)p2; p3 += fr*0.5f/SR; p3 -= (int)p3;
    float o = saw(p1, fr/SR) + saw(p2, fr/SR);
    if (!(i & 15)) fc = fco(140.0f + 2200.0f*expf(-t*14.0f));
    float v = svf(&f, o, fc, 0.5f) * 0.5f + sinf(p3*TAU)*0.5f;
    v = tanhf(v*1.4f) * env * amp;
    busL[s0+i] += v; busR[s0+i] += v;
  }
  addsync(4, s0, 10.0f, amp*1.4f);
}
static void pad(int s0, int len, const unsigned char *n, float cut0, float cut1, float amp) {
  static const float det[5] = {-0.13f, -0.06f, 0.0f, 0.07f, 0.12f};
  for (int k = 0; k < 4; k++) for (int v = 0; v < 5; v++) {
    SVF f = {0,0}; float fr = mtof(n[k] + det[v]), ph = frnd()*0.5f + 0.5f, fc = 0;
    float pan = (v - 2) * 0.35f + (k - 1.5f) * 0.1f;
    for (int i = 0; i < len + SR && s0+i < LIM; i++) {
      float t = (float)i / SR, x = (float)i / len;
      float env = fminf(1.0f, t*1.4f) * (i < len ? 1.0f : expf(-(float)(i-len)/SR*3.5f));
      ph += fr/SR; ph -= (int)ph;
      if (!(i & 31)) fc = fco((cut0 + (cut1-cut0)*fminf(x,1.0f)) * (1.0f + 0.25f*sinf(t*1.3f + k)));
      float o = svf(&f, saw(ph, fr/SR), fc, 0.9f) * env * amp * 0.05f;
      busL[s0+i] += o*(1.0f-pan); busR[s0+i] += o*(1.0f+pan); rvS[s0+i] += o*0.8f;
    }
  }
}
static void pluck(int s0, float note, float amp, float pan) {
  SVF f = {0,0}; float ph = 0, ph2 = 0.3f, fr = mtof(note), fc = 0;
  for (int i = 0; i < SR*4/10 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    ph += fr/SR; ph -= (int)ph; ph2 += fr*1.004f/SR; ph2 -= (int)ph2;
    float o = saw(ph, fr/SR) - saw(ph2 + (ph2 < 0.5f ? 0.5f : -0.5f), fr/SR);     /* pulse-ish */
    if (!(i & 15)) fc = fco(600.0f + 5000.0f*expf(-t*22.0f));
    o = svf(&f, o, fc, 0.6f) * expf(-t*9.0f) * amp;
    busL[s0+i] += o*(1.0f-pan); busR[s0+i] += o*(1.0f+pan); dlS[s0+i] += o*0.6f; rvS[s0+i] += o*0.2f;
  }
  addsync(3, s0, 8.0f, amp*4.0f);
}
static void lead(int s0, int len, float note, float amp) {
  SVF f = {0,0}; float p[3] = {0, 0.33f, 0.66f}, fr = mtof(note), fc = 0;
  for (int i = 0; i < len + SR/4 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    float vib = 1.0f + 0.006f*sinf(t*33.0f)*fminf(1.0f, fmaxf(0.0f, t-0.15f)*4.0f);
    float env = fminf(1.0f, t*40.0f) * (i < len ? 1.0f : expf(-(float)(i-len)/SR*14.0f));
    float o = 0;
    for (int k = 0; k < 3; k++) { float ff = fr*vib*(1.0f + (k-1)*0.004f); p[k] += ff/SR; p[k] -= (int)p[k]; o += saw(p[k], ff/SR); }
    if (!(i & 15)) fc = fco(1800.0f + 1800.0f*expf(-t*6.0f));
    o = svf(&f, o, fc, 0.5f) * env * amp;
    busL[s0+i] += o; busR[s0+i] += o; dlS[s0+i] += o*0.45f; rvS[s0+i] += o*0.5f;
  }
  addsync(5, s0, 3.0f, amp*4.0f);
}
static void sub(int s0, int len, float note, float amp) {
  float ph = 0, fr = mtof(note);
  for (int i = 0; i < len && s0+i < LIM; i++) {
    float x = (float)i / len, env = sinf(3.14159265f * x);
    ph += fr/SR; ph -= (int)ph;
    float v = sinf(ph*TAU) * env * amp;
    busL[s0+i] += v; busR[s0+i] += v;
  }
}
static void riser(int s0, int len) {
  SVF f = {0,0}; float fc = 0;
  for (int i = 0; i < len && s0+i < LIM; i++) {
    float x = (float)i / len;
    if (!(i & 31)) fc = fco(250.0f * powf(30.0f, x));
    svf(&f, frnd(), fc, 0.35f);
    float v = f.bp * x * x * 0.35f;
    drmL[s0+i] += v * (1.0f + 0.5f*sinf(x*40.0f)); drmR[s0+i] += v * (1.0f - 0.5f*sinf(x*40.0f)); rvS[s0+i] += v*0.6f;
    if (!(i % (SR/SYNC_HZ))) syn[7][(s0+i)/(SR/SYNC_HZ)] = x*x;
  }
}
/* formant "choir": saw through three vowel band-passes, slow vibrato */
static void choir(int s0, int len, float note, float amp, float pan) {
  static const float fm[3] = {700.0f, 1220.0f, 2600.0f}, fg[3] = {1.0f, 0.55f, 0.25f};
  SVF f[3] = {{0,0},{0,0},{0,0}}; float ph = frnd()*0.5f + 0.5f, fr = mtof(note), c[3];
  for (int k = 0; k < 3; k++) c[k] = fco(fm[k] * (1.0f + 0.05f*frnd()));
  for (int i = 0; i < len + SR && s0+i < LIM; i++) {
    float t = (float)i / SR, ff = fr * (1.0f + 0.004f*sinf(t*5.3f + note));
    float env = fminf(1.0f, t*0.8f) * (i < len ? 1.0f : expf(-(float)(i-len)/SR*3.0f));
    ph += ff/SR; ph -= (int)ph;
    float x = saw(ph, ff/SR), o = 0;
    for (int k = 0; k < 3; k++) { svf(&f[k], x, c[k], 0.12f); o += f[k].bp * fg[k]; }
    o *= env * amp;
    busL[s0+i] += o*(1.0f-pan); busR[s0+i] += o*(1.0f+pan); rvS[s0+i] += o*1.2f;
  }
}
/* inharmonic FM bell */
static void bell(int s0, float note, float amp) {
  float fr = mtof(note), pc = 0, pm = 0;
  for (int i = 0; i < SR*6 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    pm += fr*3.51f/SR; pc += fr/SR;
    float v = sinf(TAU*pc + 2.2f*expf(-t*1.5f)*sinf(TAU*pm)) * expf(-t*0.9f) * amp;
    v += sinf(TAU*pc*2.76f) * expf(-t*2.5f) * amp * 0.3f;
    drmL[s0+i] += v*0.8f; drmR[s0+i] += v; rvS[s0+i] += v*0.9f;
  }
  addsync(6, s0, 1.5f, 0.6f);
}
static void impact(int s0) {
  SVF f = {0,0}; float ph = 0, fc = 0;
  for (int i = 0; i < SR*3 && s0+i < LIM; i++) {
    float t = (float)i / SR;
    ph += (30.0f + 60.0f*expf(-t*5.0f)) / SR;
    if (!(i & 31)) fc = fco(400.0f + 4000.0f*expf(-t*4.0f));
    float v = sinf(ph*TAU)*expf(-t*1.6f)*0.9f + svf(&f, frnd(), fc, 0.8f)*expf(-t*2.5f)*0.5f;
    drmL[s0+i] += v; drmR[s0+i] += v; rvS[s0+i] += v*0.7f;
  }
  addsync(6, s0, 2.2f, 1.0f);
}

/* Freeverb-style hall + ping-pong delay, then master bus */
static void master(void) {
  float *bL = gbus[0], *bR = gbus[1], *dL = gbus[2], *dR = gbus[3], *rS = gbus[4], *dS = gbus[5];
  static const short cl[8] = {1215,1293,1390,1476,1548,1623,1695,1760}, al[4] = {605,480,371,245};   /* Freeverb's tuning, rescaled to 48 kHz */
  float *cb = (float*)calloc(2*8*1800 + 2*4*640, sizeof(float));
  float cst[16] = {0}; int ci[16] = {0}, ai[8] = {0};
  float *dbL = (float*)calloc(SR, sizeof(float)), *dbR = (float*)calloc(SR, sizeof(float));
  int dlen = SR*3/8, di = 0; float dlp = 0;
  for (int i = 0; i < NFR; i++) {
    float t = (float)i / SR; int sc = (int)(t / 16.0f); if (sc > NSEC-1) sc = NSEC-1;
    float in = rS[i] * 0.015f, out[2] = {0, 0};
    for (int c = 0; c < 2; c++) {
      for (int k = 0; k < 8; k++) {
        int L = cl[k] + c*25, id = c*8+k; float *b = cb + id*1800;
        float y = b[ci[id]];
        cst[id] = y*0.72f + cst[id]*0.28f;
        b[ci[id]] = in + cst[id]*0.88f;
        if (++ci[id] >= L) ci[id] = 0;
        out[c] += y;
      }
      for (int k = 0; k < 4; k++) {
        int L = al[k] + c*25, id = c*4+k; float *b = cb + 2*8*1800 + id*640;
        float bo = b[ai[id]]; b[ai[id]] = out[c] + bo*0.5f; out[c] = bo - out[c];
        if (++ai[id] >= L) ai[id] = 0;
      }
    }
    /* ping-pong delay, dotted eighth */
    float yl = dbL[di], yr = dbR[di];
    dlp += (yr - dlp) * 0.375f;
    dbL[di] = dS[i] + dlp*0.45f; dbR[di] = yl*0.9f;
    if (++di >= dlen) di = 0;
    /* sidechain from the kick grid */
    float duck = 1.0f;
    if (arrange[sc] & KICK) { float ph = fmodf(t*2.0f, 1.0f)*0.5f; duck = 1.0f - 0.6f*expf(-ph*9.0f)*fminf(1.0f, ph*400.0f); }
    float fade = fminf(1.0f, (SONG_SEC - t) / 6.0f);
    float L = (bL[i]*duck + dL[i] + out[0]*0.9f + yl*0.5f) * fade;
    float R = (bR[i]*duck + dR[i] + out[1]*0.9f + yr*0.5f) * fade;
    pcm[2*i]   = (short)(tanhf(L*0.75f) * 30000.0f);
    pcm[2*i+1] = (short)(tanhf(R*0.75f) * 30000.0f);
#ifdef CATHEDRA_DEV
    /* dev build: keep the unquantised master for the high-quality WAV export */
    if (pcmf) { pcmf[2*i] = tanhf(L*0.75f) * (30000.0f / 32768.0f); pcmf[2*i+1] = tanhf(R*0.75f) * (30000.0f / 32768.0f); }
#endif
  }
  free(cb); free(dbL); free(dbR);
}

static float *secw[NSEC], *secsy[NSEC];
static volatile int synth_done;           /* sections finished (+1 when mastered), for the loading bar */
static THRET section_job(void *arg) {
  int s = (int)(size_t)arg, base = s * 16 * SR;
#ifdef _WIN32
  struct tctx ctx = { 0 }; __writegsqword(0x28, (unsigned long long)&ctx);
#endif
  float *w = (float*)calloc((size_t)WIN * 6, 4);
  syn = (float(*)[NSYNC])calloc(8 * NSYNC, 4);
  if (!w || !syn) return 0;
  busL = w - base; busR = w + WIN - base; drmL = w + 2*WIN - base;
  drmR = w + 3*WIN - base; rvS = w + 4*WIN - base; dlS = w + 5*WIN - base;
  LIM = base + WIN < NFR ? base + WIN : NFR;
  rngs = 0x1234567u + s * 7919u;
  const float st = 0.125f;                      /* 16th note at 120 BPM */
  {
    unsigned f = arrange[s]; float t0 = s * 16.0f;
    if (f & IMPACT) impact(secs(t0));
    if (f & BELL) { bell(secs(t0), 57, 0.22f); bell(secs(t0 + 8.0f), 52, 0.16f); }
    if (f & RISE) riser(secs(t0 + 8.0f), secs(8.0f));
    for (int c = 0; c < 4; c++) {               /* 4 chords of 2 bars */
      const unsigned char *ch = chords[c]; float tc = t0 + c*4.0f; int last = (s == NSEC-1);
      if (f & PAD) {
        float c0 = 900, c1 = 2400;
        if (s == 0) { c0 = 250 + c*500.0f; c1 = c0 + 500; }
        if (s == 4) { c0 = 500 + c*400.0f; c1 = c0 + 400; }
        if ((s >= 5 && s <= 7) || s == 9) { c0 = 1800; c1 = 3500; }
        pad(secs(tc), secs(last && c == 3 ? 6.0f : 4.0f), ch + 1, c0, c1, last ? 0.8f : 1.0f);
      }
      if (f & SUB) sub(secs(tc), secs(4.0f), ch[0] - 12, 0.35f);
      if (f & CHOIR) for (int k = 1; k < 4; k++) choir(secs(tc), secs(4.0f), ch[k] - 12 + (k == 3 ? 12 : 0), 0.05f, (k - 2) * 0.5f);
      for (int k = 0; k < 32; k++) {           /* 16th steps */
        float tk = tc + k*st; int s0 = secs(tk), beat = k % 4 == 0;
        if ((f & KICK) && beat) kick(s0, 0.95f);
        if ((f & SNARE) && (k % 8 == 4)) snare(s0, 0.7f);
        if ((f & SNARE) && s == 6 && c == 3 && k >= 24) snare(s0, 0.25f + 0.05f*(k-24));
        if (s == 8 && c == 3 && k >= 16) snare(s0, 0.1f + 0.03f*(k-16));
        if (f & HAT) hat(s0, (k % 2 ? 0.10f : 0.16f) * (k % 4 == 2 ? 1.3f : 1.0f), 90.0f, (k % 2) ? 0.3f : -0.3f);
        if ((f & OHAT) && (k % 4 == 2)) hat(s0, 0.09f, 9.0f, 0.1f);
        if ((f & BASS) && (k % 2 == 0)) {
          static const signed char boct[16] = {0,0,12,0,0,12,0,7, 0,0,12,0,0,12,7,12};
          bass(s0, secs(st*1.6f), ch[0] - 12 + boct[(k/2) % 16], 0.55f);
        }
        if (f & ARP) {
          int idx = arpPat[k % 16]; float a = (s == 1 || s == 8) ? 0.08f : 0.12f;
          if (s == 4) a = 0.05f + 0.08f * (float)c / 3.0f;
          pluck(s0, ch[1 + idx % 4] + 12 * (idx / 4) + 12, a, (k % 2) ? 0.4f : -0.4f);
        }
      }
    }
    if (f & LEAD) for (unsigned m = 0; m < sizeof(melody)/3; m++)
      lead(secs(t0 + melody[m][0]*st), secs(melody[m][1]*st*0.95f), melody[m][2] + (s >= 6 ? 12 : 0) - 12, s == 9 ? 0.12f : 0.11f);
  }
  if (s == 4) for (int k = 0; k < 16; k++) snare(secs(78.0f + k*0.125f), 0.12f + 0.03f*k);  /* roll into the drop */
  secw[s] = w; secsy[s] = &syn[0][0];
  __sync_fetch_and_add(&synth_done, 1);
  return 0;
}

static void synth_render(void) {
  thr_t th[NSEC];
  syncv = (float(*)[NSYNC])calloc(8 * NSYNC, 4);
  for (int b = 0; b < 6; b++) gbus[b] = (float*)calloc(NFR, 4);
  pcm = (short*)calloc((size_t)NFR*2, 2);
  for (int s = 0; s < NSEC; s++) th[s] = thr_go(section_job, (void*)(size_t)s);
  for (int s = 0; s < NSEC; s++) {
    thr_join(th[s]);
    if (!secw[s]) continue;
    int base = s * 16 * SR, n = base + WIN < NFR ? WIN : NFR - base;
    for (int b = 0; b < 6; b++) for (int i = 0; i < n; i++) gbus[b][base + i] += secw[s][b*WIN + i];
    { float *sv = &syncv[0][0]; for (int i = 0; i < 8*NSYNC; i++) if (secsy[s][i] > sv[i]) sv[i] = secsy[s][i]; }
    free(secw[s]); free(secsy[s]);
  }
  master();
  synth_done++;
  for (int b = 0; b < 6; b++) free(gbus[b]);
}
