/*  CATHEDRA - a 64k intro.
 *  Linux (X11 + ALSA) is the packed release. The same sources also build a Win32
 *  window with waveOut. Vulkan 1.3 + hardware ray tracing.
 *
 *  Heavy GPU precalc at startup (progress bar): mipmapped Perlin-Worley noise,
 *  a cloud-deck volume (density + sun optical depth, up to 2048x128x2048), the
 *  planet's signed-distance/heat/strata volume (up to 640^3), a world map, a
 *  multi-million element Dyson swarm and dense gothic architecture: a grammar
 *  emits parts on the CPU, a compute shader expands them into meshes
 *  (towers, flying buttresses, tracery, pinnacles, scaffolds, rocks)
 *  which get their own BLASes. Per frame: ray-queried/ray-marched scene,
 *  instanced swarm, temporal AA with jittered supersampling, then post.
 *
 *  Release build (cathedra): fullscreen at desktop resolution,
 *  Esc quits, Space pauses - nothing else.
 *  Dev build (-DCATHEDRA_DEV, cathedra_dev): same visuals plus
 *    -w windowed 1280x720 | -s <scale> fixed render scale | --tier <0-3>
 *    --selftest | --shot <t> (repeatable) | --frames <n> | --size <W>x<H>
 *    --taa <n> TAA lead-in frames per shot | --sweep (per-second timing sweep)
 *    --raw <fps> <t0> <t1> (raw RGB24 frames to stdout) | --panels <n>
 *    --from <s> --to <s> | --section <0-10> | --loop  (playback range, Linux; also the --export range)
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#define VK_USE_PLATFORM_WIN32_KHR
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <time.h>
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) && defined(TINY_CRT)
/* call msvcrt.dll's float math directly: mingw links its own static exp/sin/sincos (~1.5 KB) otherwise */
extern float (*__imp_expf)(float), (*__imp_sinf)(float), (*__imp_cosf)(float), (*__imp_powf)(float, float);
#define expf(x) __imp_expf(x)
#define sinf(x) __imp_sinf(x)
#define cosf(x) __imp_cosf(x)
#define powf(x, y) __imp_powf(x, y)
#endif

#include "synth.h"
#include "spirv.h"
#include "font.h"
#include "arch.h"

#ifdef _WIN32
#define SURF_FN X(vkCreateWin32SurfaceKHR)
#else
#define SURF_FN X(vkCreateXlibSurfaceKHR)
#endif
#define VKFUNCS \
  X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceMemoryProperties) X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) \
  X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) SURF_FN \
  X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) \
  X(vkQueuePresentKHR) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
  X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
  X(vkBindBufferMemory) X(vkMapMemory) X(vkGetBufferDeviceAddress) X(vkCreateSampler) X(vkCreateShaderModule) \
  X(vkCreateDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkCreateGraphicsPipelines) X(vkCreateComputePipelines) \
  X(vkCreateDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateCommandPool) \
  X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkDeviceWaitIdle) \
  X(vkCreateFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkCmdPipelineBarrier2) X(vkCmdBeginRendering) \
  X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDraw) X(vkCmdDispatch) \
  X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdBlitImage) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) \
  X(vkCmdClearAttachments) X(vkCreateQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults) \
  X(vkCreateAccelerationStructureKHR) X(vkGetAccelerationStructureBuildSizesKHR) X(vkCmdBuildAccelerationStructuresKHR) \
  X(vkGetAccelerationStructureDeviceAddressKHR)
/* the entry points live in one struct that the loader fills as an array (no per-function
   pointer table in the 64k). A name added to VKFUNCS also needs its #define below,
   otherwise the build fails with "undeclared". */
#define X(n) PFN_##n n;
static struct { VKFUNCS } vkf;
#undef X
#define X(n) #n "\0"
static const char vk_names[] = VKFUNCS;
#undef X
#define vkCreateWin32SurfaceKHR vkf.vkCreateWin32SurfaceKHR
#define vkCreateXlibSurfaceKHR vkf.vkCreateXlibSurfaceKHR
#define vkEnumeratePhysicalDevices vkf.vkEnumeratePhysicalDevices
#define vkGetPhysicalDeviceProperties vkf.vkGetPhysicalDeviceProperties
#define vkGetPhysicalDeviceQueueFamilyProperties vkf.vkGetPhysicalDeviceQueueFamilyProperties
#define vkGetPhysicalDeviceMemoryProperties vkf.vkGetPhysicalDeviceMemoryProperties
#define vkEnumerateDeviceExtensionProperties vkf.vkEnumerateDeviceExtensionProperties
#define vkCreateDevice vkf.vkCreateDevice
#define vkGetPhysicalDeviceSurfaceSupportKHR vkf.vkGetPhysicalDeviceSurfaceSupportKHR
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkf.vkGetPhysicalDeviceSurfaceCapabilitiesKHR
#define vkGetPhysicalDeviceSurfaceFormatsKHR vkf.vkGetPhysicalDeviceSurfaceFormatsKHR
#define vkGetDeviceQueue vkf.vkGetDeviceQueue
#define vkCreateSwapchainKHR vkf.vkCreateSwapchainKHR
#define vkDestroySwapchainKHR vkf.vkDestroySwapchainKHR
#define vkGetSwapchainImagesKHR vkf.vkGetSwapchainImagesKHR
#define vkAcquireNextImageKHR vkf.vkAcquireNextImageKHR
#define vkQueuePresentKHR vkf.vkQueuePresentKHR
#define vkCreateImage vkf.vkCreateImage
#define vkDestroyImage vkf.vkDestroyImage
#define vkGetImageMemoryRequirements vkf.vkGetImageMemoryRequirements
#define vkAllocateMemory vkf.vkAllocateMemory
#define vkFreeMemory vkf.vkFreeMemory
#define vkBindImageMemory vkf.vkBindImageMemory
#define vkCreateImageView vkf.vkCreateImageView
#define vkDestroyImageView vkf.vkDestroyImageView
#define vkCreateBuffer vkf.vkCreateBuffer
#define vkDestroyBuffer vkf.vkDestroyBuffer
#define vkGetBufferMemoryRequirements vkf.vkGetBufferMemoryRequirements
#define vkBindBufferMemory vkf.vkBindBufferMemory
#define vkMapMemory vkf.vkMapMemory
#define vkGetBufferDeviceAddress vkf.vkGetBufferDeviceAddress
#define vkCreateSampler vkf.vkCreateSampler
#define vkCreateShaderModule vkf.vkCreateShaderModule
#define vkCreateDescriptorSetLayout vkf.vkCreateDescriptorSetLayout
#define vkCreatePipelineLayout vkf.vkCreatePipelineLayout
#define vkCreateGraphicsPipelines vkf.vkCreateGraphicsPipelines
#define vkCreateComputePipelines vkf.vkCreateComputePipelines
#define vkCreateDescriptorPool vkf.vkCreateDescriptorPool
#define vkAllocateDescriptorSets vkf.vkAllocateDescriptorSets
#define vkUpdateDescriptorSets vkf.vkUpdateDescriptorSets
#define vkCreateCommandPool vkf.vkCreateCommandPool
#define vkAllocateCommandBuffers vkf.vkAllocateCommandBuffers
#define vkBeginCommandBuffer vkf.vkBeginCommandBuffer
#define vkEndCommandBuffer vkf.vkEndCommandBuffer
#define vkQueueSubmit vkf.vkQueueSubmit
#define vkDeviceWaitIdle vkf.vkDeviceWaitIdle
#define vkCreateFence vkf.vkCreateFence
#define vkWaitForFences vkf.vkWaitForFences
#define vkResetFences vkf.vkResetFences
#define vkCreateSemaphore vkf.vkCreateSemaphore
#define vkCmdPipelineBarrier2 vkf.vkCmdPipelineBarrier2
#define vkCmdBeginRendering vkf.vkCmdBeginRendering
#define vkCmdEndRendering vkf.vkCmdEndRendering
#define vkCmdBindPipeline vkf.vkCmdBindPipeline
#define vkCmdBindDescriptorSets vkf.vkCmdBindDescriptorSets
#define vkCmdPushConstants vkf.vkCmdPushConstants
#define vkCmdDraw vkf.vkCmdDraw
#define vkCmdDispatch vkf.vkCmdDispatch
#define vkCmdSetViewport vkf.vkCmdSetViewport
#define vkCmdSetScissor vkf.vkCmdSetScissor
#define vkCmdBlitImage vkf.vkCmdBlitImage
#define vkCmdCopyBufferToImage vkf.vkCmdCopyBufferToImage
#define vkCmdCopyImageToBuffer vkf.vkCmdCopyImageToBuffer
#define vkCmdClearAttachments vkf.vkCmdClearAttachments
#define vkCreateQueryPool vkf.vkCreateQueryPool
#define vkCmdResetQueryPool vkf.vkCmdResetQueryPool
#define vkCmdWriteTimestamp vkf.vkCmdWriteTimestamp
#define vkGetQueryPoolResults vkf.vkGetQueryPoolResults
#define vkCreateAccelerationStructureKHR vkf.vkCreateAccelerationStructureKHR
#define vkGetAccelerationStructureBuildSizesKHR vkf.vkGetAccelerationStructureBuildSizesKHR
#define vkCmdBuildAccelerationStructuresKHR vkf.vkCmdBuildAccelerationStructuresKHR
#define vkGetAccelerationStructureDeviceAddressKHR vkf.vkGetAccelerationStructureDeviceAddressKHR
static PFN_vkGetInstanceProcAddr gipa;

static void fatal(const char *msg);
#ifdef CATHEDRA_DEV
static void ckf(int line, VkResult r) {
  char m[96]; snprintf(m, sizeof m, "Vulkan error %d (main.c:%d). A Vulkan 1.3 GPU with ray query support is required.", (int)r, line);
  fatal(m);
}
#define CK(x) do { VkResult r_ = (x); if (r_ < 0) ckf(__LINE__, r_); } while (0)
#else
static void ckf(void) { fatal("A Vulkan 1.3 GPU with ray tracing (ray query) is required."); }
#define CK(x) do { if ((x) < 0) ckf(); } while (0)
#endif

static int W = 1280, H = 720, quit_req, paused, windowed, resized;
#ifdef CATHEDRA_DEV
/* tiny number parser (keeps the C runtime's float parsing out of the binary) */
static float num(const char *s) {
  float v = 0, f = 0, sg = 1;
  if (*s == '-') { sg = -1; s++; }
  for (; *s; s++) {
    if (*s == '.') f = 1;
    else if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); f *= 10; }
    else break;
  }
  return sg * (f > 0 ? v / f : v);
}
#endif

/* Rasterise the caption strings with the baked font, then turn them into a
 * signed distance field so the titles stay razor sharp at any resolution. */
#define TXW 2048
#define LH  96
#define TXH (LH * NLINES)
#define SDR 8
static void build_text(unsigned char *out) {
  unsigned char *bm = (unsigned char *)calloc(TXW * TXH, 1);
  int goff[64]; int o = 0;
  for (int g = 0; font_chars[g]; g++) { goff[g] = o; o += (font_w[g] + 7) / 8 * FONT_H; }   /* columns 8 px wide, each top to bottom */
  int xr[NLINES][2];
  for (int l = 0; l < NLINES; l++) {
    const char *s = txt_lines[l]; int wsum = 0;
    for (int pass = 0; pass < 2; pass++) {
      int x = (TXW - wsum) / 2;
      for (const char *c = s; *c; c++) {
        const char *f = strchr(font_chars, *c);
        if (*c == ' ' || !f) { x += FONT_SPACE; continue; }
        int g = (int)(f - font_chars), w = font_w[g];
        if (pass) for (int y = 0; y < FONT_H; y++) for (int i = 0; i < w; i++) {
          if ((font_bits[goff[g] + i / 8 * FONT_H + y] >> (i & 7)) & 1) {
            int px = x + i, py = l * LH + (LH - FONT_H) / 2 + (FONT_H - 1 - y);
            if (px >= 0 && px < TXW) bm[py * TXW + px] = 1;
          }
        }
        x += w + FONT_TRACK;
      }
      if (pass) { xr[l][0] = (TXW - wsum) / 2 - SDR - 1; xr[l][1] = x + SDR + 1; }
      wsum = x - (TXW - wsum) / 2;
    }
  }
  memset(out, 0, TXW * TXH);
  for (int y = 0; y < TXH; y++) for (int x = xr[y / LH][0]; x < xr[y / LH][1]; x++) {
    if (x < 0 || x >= TXW) continue;
    int in = bm[y * TXW + x], best = SDR * SDR * 2;
    for (int dy = -SDR; dy <= SDR; dy++) for (int dx = -SDR; dx <= SDR; dx++) {
      int yy = y + dy, xx = x + dx, d2 = dx * dx + dy * dy;
      if (d2 < best && yy >= 0 && yy < TXH && xx >= 0 && xx < TXW && bm[yy * TXW + xx] != in) best = d2;
    }
    float d = sqrtf((float)best) - 0.5f; if (d > SDR) d = SDR;
    float v = 0.5f + (in ? d : -d) / (2.0f * SDR);
    out[y * TXW + x] = (unsigned char)(fminf(fmaxf(v, 0.0f), 1.0f) * 255.0f);
  }
  free(bm);
}

/* --- platform layer ------------------------------------------------------ */
static double now_sec(void);
static double audio_time(void);
static void audio_pause(int p);
static void pump(void);

#ifdef _WIN32
static HWND hwnd; static HINSTANCE hinst; static HWAVEOUT hwo; static WAVEHDR whdr; static int have_audio;
static LARGE_INTEGER qf;
static double now_sec(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / qf.QuadPart; }
#ifdef CATHEDRA_DEV
static void fatal(const char *msg) { fprintf(stderr, "CATHEDRA: %s\n", msg); MessageBoxA(0, msg, "CATHEDRA", MB_OK | MB_ICONERROR); ExitProcess(1); }
#else
static void fatal(const char *msg) { MessageBoxA(0, msg, "CATHEDRA", MB_ICONERROR); ExitProcess(1); }
#endif
static void *vk_lib(void) { HMODULE m = LoadLibraryA("vulkan-1.dll"); return m ? (void *)GetProcAddress(m, "vkGetInstanceProcAddr") : 0; }
static void nap(void) { Sleep(4); }
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_KEYDOWN) { if (w == VK_ESCAPE) quit_req = 1; if (w == VK_SPACE) { paused ^= 1; audio_pause(paused); } return 0; }
  if (m == WM_CLOSE || m == WM_DESTROY) { quit_req = 1; return 0; }
  if (m == WM_SIZE) { W = LOWORD(l); H = HIWORD(l); resized = 1; }
  if (m == WM_SETCURSOR && !windowed) { SetCursor(0); return 1; }
  return DefWindowProcA(h, m, w, l);
}
static void platform_init(void) {
  SetProcessDPIAware();
  if (!windowed) { W = GetSystemMetrics(SM_CXSCREEN); H = GetSystemMetrics(SM_CYSCREEN); }
  WNDCLASSA wc = {0}; wc.lpfnWndProc = wndproc; wc.hInstance = hinst = GetModuleHandleA(0); wc.lpszClassName = "CATHEDRA";
  wc.hCursor = LoadCursor(0, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClassA(&wc);
  DWORD st = windowed ? (WS_OVERLAPPEDWINDOW | WS_VISIBLE) : (WS_POPUP | WS_VISIBLE);
  RECT r = {0, 0, W, H}; AdjustWindowRect(&r, st, 0);
  hwnd = CreateWindowExA(0, "CATHEDRA", "CATHEDRA", st, windowed ? CW_USEDEFAULT : 0, windowed ? CW_USEDEFAULT : 0,
                         r.right - r.left, r.bottom - r.top, 0, 0, hinst, 0);
  if (!windowed) ShowCursor(FALSE);
}
static VkResult mksurface(VkInstance in, VkSurfaceKHR *s) {
  VkWin32SurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, 0, 0, hinst, hwnd };
  return vkCreateWin32SurfaceKHR(in, &ci, 0, s);
}
#define SURF_EXT "VK_KHR_win32_surface"
static void audio_start(void) {
  WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, SR, SR * 4, 4, 16, 0 };
  if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return;
  whdr.lpData = (LPSTR)pcm; whdr.dwBufferLength = NFR * 4;
  waveOutPrepareHeader(hwo, &whdr, sizeof whdr); waveOutWrite(hwo, &whdr, sizeof whdr);
  have_audio = 1;
}
static double t_clock0, t_pause_acc, t_pause_at;
static double audio_time(void) {
  if (have_audio) {
    MMTIME mt; mt.wType = TIME_SAMPLES; waveOutGetPosition(hwo, &mt, sizeof mt);
    return mt.wType == TIME_BYTES ? mt.u.cb / 4.0 / SR : (double)mt.u.sample / SR;
  }
  return (paused ? t_pause_at : now_sec()) - t_clock0 - t_pause_acc;
}
static void audio_pause(int p) {
  if (have_audio) { if (p) waveOutPause(hwo); else waveOutRestart(hwo); return; }
  if (p) t_pause_at = now_sec(); else t_pause_acc += now_sec() - t_pause_at;
}
static void pump(void) { MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); } }
static void platform_quit(void) { if (have_audio) { waveOutReset(hwo); waveOutClose(hwo); } }
#else
static Display *dpy; static Window win; static Atom wm_del;
static snd_pcm_t *pcmh; static thr_t athr; static int have_audio;
static volatile int aseq, acur; static volatile double apos, astamp;
#ifdef CATHEDRA_DEV
static volatile int aseek = -1; static int play_loop;   /* dev playback: audio seek request (frame), --loop */
#endif
static double now_sec(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static void fatal(const char *msg) { fprintf(stderr, "CATHEDRA: %s\n", msg); exit(1); }
static void *vk_lib(void) {
  void *l = dlopen("libvulkan.so.1", RTLD_NOW); if (!l) l = dlopen("libvulkan.so", RTLD_NOW);
  return l ? dlsym(l, "vkGetInstanceProcAddr") : 0;
}
static void nap(void) { struct timespec ts = { 0, 4000000 }; nanosleep(&ts, 0); }
static void platform_init(void) {
  dpy = XOpenDisplay(0);
  if (!dpy) fatal("cannot open X display");
  int scr = DefaultScreen(dpy);
  if (!windowed) { W = DisplayWidth(dpy, scr); H = DisplayHeight(dpy, scr); }
  XSetWindowAttributes swa; memset(&swa, 0, sizeof swa);
  swa.event_mask = KeyPressMask | StructureNotifyMask;
  win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, CopyFromParent, InputOutput, CopyFromParent,
                      CWEventMask | CWBorderPixel, &swa);   /* no background: X never paints black over our frames */
  XStoreName(dpy, win, "CATHEDRA");
  wm_del = XInternAtom(dpy, "WM_DELETE_WINDOW", 0); XSetWMProtocols(dpy, win, &wm_del, 1);
  if (!windowed) {
    Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", 0);
    XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_STATE", 0), XA_ATOM, 32, PropModeReplace, (unsigned char *)&fs, 1);
    static char z[8]; XColor c; memset(&c, 0, sizeof c);
    Pixmap pm = XCreateBitmapFromData(dpy, win, z, 8, 8);
    XDefineCursor(dpy, win, XCreatePixmapCursor(dpy, pm, pm, &c, &c, 0, 0));
  }
  XMapWindow(dpy, win);
  /* wait until the window manager has mapped (and fullscreened) the window, so the swapchain is
     created once at the final size instead of being rebuilt during the first seconds */
  XEvent e; do { XNextEvent(dpy, &e); if (e.type == ConfigureNotify) { W = e.xconfigure.width; H = e.xconfigure.height; } } while (e.type != MapNotify);
}
static VkResult mksurface(VkInstance in, VkSurfaceKHR *s) {
  VkXlibSurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, 0, 0, dpy, win };
  return vkCreateXlibSurfaceKHR(in, &ci, 0, s);
}
#define SURF_EXT "VK_KHR_xlib_surface"
static void *athread(void *arg) {
  static short sil[2048];
  (void)arg;
#ifdef CATHEDRA_DEV
  while (!quit_req && (acur < NFR || play_loop)) {
    if (aseek >= 0) {   /* dev seek: drop what is queued, continue at the requested frame */
      snd_pcm_drop(pcmh); snd_pcm_prepare(pcmh); acur = aseek;
      aseq++; __sync_synchronize(); apos = (double)acur / SR; astamp = now_sec(); __sync_synchronize(); aseq++;
      aseek = -1;
    }
    int n = 1024; snd_pcm_sframes_t r;
    if (paused || acur >= NFR) r = snd_pcm_writei(pcmh, sil, n);
#else
  while (!quit_req && acur < NFR) {
    int n = 1024; snd_pcm_sframes_t r;
    if (paused) r = snd_pcm_writei(pcmh, sil, n);
#endif
    else { if (acur + n > NFR) n = NFR - acur; r = snd_pcm_writei(pcmh, pcm + 2 * acur, n); if (r > 0) acur += (int)r; }
    if (r < 0) { snd_pcm_recover(pcmh, (int)r, 1); continue; }
    snd_pcm_sframes_t del = 0; if (snd_pcm_delay(pcmh, &del) < 0) del = 0;
    aseq++; __sync_synchronize();
    apos = (double)(acur - del) / SR; astamp = now_sec();
    __sync_synchronize(); aseq++;
  }
  return 0;
}
static double t_clock0, t_pause_acc, t_pause_at, t_last;
static void audio_start(void) {
#ifdef CATHEDRA_DEV
  if (getenv("CATHEDRA_NOAUDIO")) return;   /* dev switch: play without sound */
#endif
  if (snd_pcm_open(&pcmh, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) return;
  if (snd_pcm_set_params(pcmh, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, SR, 1, 60000) < 0) return;
  astamp = now_sec(); have_audio = 1;
  athr = thr_go(athread, 0);
}
static double audio_time(void) {
  double t;
  if (have_audio) {
    int s; double p, st;
    do { s = aseq; __sync_synchronize(); p = apos; st = astamp; __sync_synchronize(); } while ((s & 1) || s != aseq);
    t = paused ? p : p + (now_sec() - st);
  } else t = (paused ? t_pause_at : now_sec()) - t_clock0 - t_pause_acc;
  if (!paused && t < t_last) t = t_last;   /* never run backwards */
  t_last = t;
  return t;
}
static void audio_pause(int p) {
  if (have_audio) return;                  /* the audio thread feeds silence while paused */
  if (p) t_pause_at = now_sec(); else t_pause_acc += now_sec() - t_pause_at;
}
static void pump(void) {
  while (XPending(dpy)) {
    XEvent e; XNextEvent(dpy, &e);
    if (e.type == KeyPress) {
      KeySym k = XLookupKeysym(&e.xkey, 0);
      if (k == XK_Escape) quit_req = 1;
      if (k == XK_space) { paused ^= 1; audio_pause(paused); }
    } else if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wm_del) quit_req = 1;
    else if (e.type == ConfigureNotify && (e.xconfigure.width != W || e.xconfigure.height != H)) {
      W = e.xconfigure.width; H = e.xconfigure.height; resized = 1;
    }
  }
}
static void platform_quit(void) {
  quit_req = 1;
  if (have_audio) { thr_join(athr); snd_pcm_drop(pcmh); snd_pcm_close(pcmh); }
}
#endif
static THRET synth_job(void *a) { (void)a; synth_render(); return 0; }

/* --- Vulkan helpers ------------------------------------------------------- */
typedef struct { VkBuffer b; VkDeviceMemory m; unsigned char *p; VkDeviceAddress a; VkDeviceSize n; } Buf;
typedef struct { VkImage i; VkDeviceMemory m; VkImageView v, v0; } Img;
typedef struct { VkAccelerationStructureKHR h; Buf b; VkDeviceAddress a; } Acc;
static VkInstance vki; static VkPhysicalDevice gpu; static VkDevice dev; static VkQueue queue; static uint32_t qfam;
static VkSurfaceKHR surf; static VkPhysicalDeviceMemoryProperties mprops; static VkPhysicalDeviceProperties gprops;
static VkCommandBuffer cmd; static VkFence fence; static VkSemaphore sem_acq, sem_done[8]; static VkQueryPool qpool;
static VkDescriptorSet dset; static VkPipelineLayout playout;
static VkPipeline pip_scene, pip_swarm, pip_post, pip_taa, pcomp[9];
/* compute passes = specialisations of the one bake.comp module (MODE, workgroup size) */
#define pip_noise pcomp[0]
#define pip_bswarm pcomp[1]
#define pip_rtv pcomp[2]
#define pip_mesh pcomp[3]
#define pip_cloud pcomp[4]
#define pip_planet pcomp[5]
#define pip_pmap pcomp[6]
#define pip_chunks pcomp[7]
#define pip_cam pcomp[8]
static VkSampler smp_rep, smp_clamp;
static VkFormat out_fmt = VK_FORMAT_B8G8R8A8_UNORM;
static VkSwapchainKHR swc; static uint32_t nsc; static VkImage sci[8]; static VkImageView scv[8];
static int headless, has_rtp, has_mesh, soft, afail;
static Img C, Z, Hh[2]; static int fw, fh, cmips, hcur;
static Buf scratch;
static Acc blas_ring, tlasA, tlasB;
static VkAccelerationStructureGeometryKHR g_tb;
static int npanels, nch, ntb;
static int tier = -1;
/* quality tiers: cloud volume xyz, planet volume, planet map, tower variants, swarm elements, debris chunks */
static const int tiers[4][8] = {
  { 256, 24, 256, 160, 1024, 6, 1 << 17, 1024 },
  { 768, 48, 768, 320, 2048, 16, 1 << 18, 4096 },
  { 1536, 96, 1536, 512, 4096, 32, 1 << 20, 16384 },
  { 2048, 128, 2048, 640, 8192, 48, 1 << 22, 32768 } };

#define BUF_USAGE (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | \
  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
#define HOSTMEM (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
#define COL VK_IMAGE_ASPECT_COLOR_BIT
#define L_UND VK_IMAGE_LAYOUT_UNDEFINED
#define L_GEN VK_IMAGE_LAYOUT_GENERAL
#define L_ATT VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
#define L_RO VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
#define L_SRC VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
#define L_DST VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
#define CIS VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
#define RGBA16 VK_FORMAT_R16G16B16A16_SFLOAT

/* soft mode: a failed allocation of one of the big precalc volumes only sets afail (-> lower tier) */
static VkDeviceMemory memalloc(VkMemoryRequirements r, VkMemoryPropertyFlags f) {
  uint32_t i = 0;
  while (i < mprops.memoryTypeCount && (!((r.memoryTypeBits >> i) & 1) || (mprops.memoryTypes[i].propertyFlags & f) != f)) i++;
  if (i == mprops.memoryTypeCount) fatal("no suitable GPU memory type");
  VkMemoryAllocateFlagsInfo fi = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, 0, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, 0 };
  VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &fi, r.size, i };
  VkDeviceMemory m = 0; VkResult e = vkAllocateMemory(dev, &ai, 0, &m);
  if (e < 0) { if (soft) { afail = 1; return 0; } CK(e); }
  return m;
}
static Buf mkbuf(VkDeviceSize n, int host) {
  Buf B; memset(&B, 0, sizeof B); B.n = n;
  VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, n, BUF_USAGE, 0, 0, 0 };
  CK(vkCreateBuffer(dev, &ci, 0, &B.b));
  VkMemoryRequirements r; vkGetBufferMemoryRequirements(dev, B.b, &r);
  if (!(B.m = memalloc(r, host ? HOSTMEM : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) return B;
  CK(vkBindBufferMemory(dev, B.b, B.m, 0));
  if (host) CK(vkMapMemory(dev, B.m, 0, n, 0, (void **)&B.p));
  VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, 0, B.b };
  B.a = vkGetBufferDeviceAddress(dev, &ai);
  return B;
}
static void freebuf(Buf *B) { vkDestroyBuffer(dev, B->b, 0); if (B->m) vkFreeMemory(dev, B->m, 0); memset(B, 0, sizeof *B); }
static Img mkimg(int d3, VkFormat f, uint32_t w, uint32_t h, uint32_t d, uint32_t mips, VkImageUsageFlags u) {
  Img I; memset(&I, 0, sizeof I);
  VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, 0, 0, d3 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D, f, { w, h, d }, mips, 1,
    VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, u, 0, 0, 0, L_UND };
  CK(vkCreateImage(dev, &ci, 0, &I.i));
  VkMemoryRequirements r; vkGetImageMemoryRequirements(dev, I.i, &r);
  if (!(I.m = memalloc(r, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) return I;
  CK(vkBindImageMemory(dev, I.i, I.m, 0));
  VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 0, 0, I.i, d3 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D, f,
    { 0, 0, 0, 0 }, { f == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : COL, 0, mips, 0, 1 } };
  CK(vkCreateImageView(dev, &vi, 0, &I.v));
  vi.subresourceRange.levelCount = 1;
  CK(vkCreateImageView(dev, &vi, 0, &I.v0));
  return I;
}
static void freeimg(Img *I) {
  if (I->v) { vkDestroyImageView(dev, I->v, 0); vkDestroyImageView(dev, I->v0, 0); }
  vkDestroyImage(dev, I->i, 0); if (I->m) vkFreeMemory(dev, I->m, 0); memset(I, 0, sizeof *I);
}
static void wr(uint32_t b, VkDescriptorType t, const VkDescriptorImageInfo *ii, const VkDescriptorBufferInfo *bi, const void *nx) {
  VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nx, dset, b, 0, 1, t, ii, bi, 0 };
  vkUpdateDescriptorSets(dev, 1, &w, 0, 0);
}
static void set_img(uint32_t b, VkDescriptorType t, VkSampler s, VkImageView v, VkImageLayout l) {
  VkDescriptorImageInfo ii = { s, v, l }; wr(b, t, &ii, 0, 0);
}
/* sampled view at binding b, storage view (level 0) at b+1 */
static void set_vol(uint32_t b, Img *I, VkSampler s) {
  set_img(b, CIS, s, I->v, L_GEN); set_img(b + 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, I->v0, L_GEN);
}
static void set_buf(uint32_t b, Buf *B) { VkDescriptorBufferInfo bi = { B->b, 0, VK_WHOLE_SIZE }; wr(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &bi, 0); }
static void set_as(uint32_t b, Acc *A) {
  VkWriteDescriptorSetAccelerationStructureKHR a = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, 0, 1, &A->h };
  wr(b, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 0, 0, &a);
}
static void cbegin(void) { VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, 0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0 }; CK(vkBeginCommandBuffer(cmd, &bi)); }
static void csubmit(VkSemaphore wait, VkSemaphore sig) {
  CK(vkEndCommandBuffer(cmd));
  VkPipelineStageFlags ws = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, 0, wait ? 1u : 0u, &wait, &ws, 1, &cmd, sig ? 1u : 0u, &sig };
  CK(vkResetFences(dev, 1, &fence)); CK(vkQueueSubmit(queue, 1, &si, fence));
}
static void cwait(void) { CK(vkWaitForFences(dev, 1, &fence, 1, ~0ull)); }
static void ibar(VkImage im, VkImageLayout a, VkImageLayout b, uint32_t mip, uint32_t n, VkImageAspectFlags asp) {
  VkImageMemoryBarrier2 m = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, a, b,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im, { asp, mip, n, 0, 1 } };
  VkDependencyInfo d = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 0, 0, 0, 0, 0, 0, 1, &m };
  vkCmdPipelineBarrier2(cmd, &d);
}
static void mbar(void) {
  VkMemoryBarrier2 m = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };
  VkDependencyInfo d = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 0, 0, 1, &m, 0, 0, 0, 0 };
  vkCmdPipelineBarrier2(cmd, &d);
}
/* downsample chain with linear blits (2D or 3D); leaves every level in layout 'to' */
static void mips(VkImage im, int w, int h, int d, int n, VkImageLayout from, VkImageLayout to) {
  ibar(im, from, L_SRC, 0, 1, COL);
  if (n > 1) ibar(im, L_UND, L_DST, 1, n - 1, COL);
  for (int i = 1; i < n; i++) {
    int w2 = w > 1 ? w / 2 : 1, h2 = h > 1 ? h / 2 : 1, d2 = d > 1 ? d / 2 : 1;
    VkImageBlit b = { { COL, i - 1, 0, 1 }, { { 0, 0, 0 }, { w, h, d } }, { COL, i, 0, 1 }, { { 0, 0, 0 }, { w2, h2, d2 } } };
    vkCmdBlitImage(cmd, im, L_SRC, im, L_DST, 1, &b, VK_FILTER_LINEAR);
    ibar(im, L_DST, L_SRC, i, 1, COL);
    w = w2; h = h2; d = d2;
  }
  ibar(im, L_SRC, to, 0, n, COL);
}
static VkShaderModule shmod(const unsigned *c, size_t n) {
  VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, n, c };
  VkShaderModule m; CK(vkCreateShaderModule(dev, &ci, 0, &m)); return m;
}
/* the shaders ship re-encoded (tools/spvpack.py: varints, result-relative ids,
   remapped opcodes, compact swizzles, decimal constants - SMOL-V style); unspv() restores the SPIR-V */
static const unsigned char *sp_p[9];   /* field streams: hdr, op, type, res, rel, swz, var, raw, constants */
static uint32_t *spv_w[NSPV], spv_n[NSPV];
static uint32_t rv(int k) { uint32_t v = 0, c; int s = 0; do { c = *sp_p[k]++; v |= (c & 127) << s; s += 7; } while (c & 128); return v; }
static uint32_t zd(uint32_t u) { return u & 1 ? ~(u >> 1) : u >> 1; }
static uint32_t rd4(int k) { uint32_t v; memcpy(&v, sp_p[k], 4); sp_p[k] += 4; return v; }
static void unspv(void) {
  static const unsigned char swp[16] = { 80, 61, 62, 136, 131, 81, 245, 250, 248, 142, 133, 129, 12, 13, 57, 249 };
  for (int k = 0; k < 9; k++) sp_p[k] = spv_pack + spv_ofs[k];
  for (int m = 0; m < NSPV; m++) {
    uint32_t nb = rd4(0), *w = (uint32_t *)malloc((nb + 5) * 4), *o = w + 5, *e = o + nb, prev = 0, v;
    w[0] = 0x07230203; for (int i = 1; i < 5; i++) w[i] = rd4(0);
    while (o < e) {
      uint32_t x = rv(1), L = ((x >> 20) << 4) | ((x >> 4) & 15), op = ((x >> 4) & 0xfff0) | (x & 15), f, *ins = o, swz = 0;
      if (op < 16) op = swp[op]; else for (int i = 0; i < 16; i++) if (swp[i] == op) op = i;
      if (op == 13) { op = 79; swz = 1; }
      L += 1 + (op == 79 ? 4 : 0) + (op == 61 || op == 65 ? 3 : 0) + (op == 71 ? 2 : 0);
      *o++ = L << 16 | op; f = op < NOPS ? smol_ops[op] : 0;
      if (f & 2) *o++ = rv(2);
      if (f & 1) *o++ = prev += zd(rv(3));
      if (op == 43 && L == 4) {   /* 32-bit constant: a short decimal n / 10^k (exact in float32), or 0 and the raw bits */
        float c = 1; v = rv(8);
        for (uint32_t k = v & 15; k--; ) c *= 10;
        c = (int)zd(v >> 4) / c;
        if (v) memcpy(o++, &c, 4); else *o++ = rd4(7);
      }
      if (op == 12) { *o++ = 1; *o++ = rv(6); f |= 31 << 3; }   /* OpExtInst: GLSL.std.450 (id 1), opcode, then ids */
      /* operand ids: absolute (2*id+1) or result-relative (2*zigzag(result - id)), the encoder's choice */
      for (uint32_t k = f >> 3; k && o < ins + L; k--) v = rv(4), *o++ = v & 1 ? v >> 1 : prev - zd(v >> 1);
      if (swz) { uint32_t c = *sp_p[5]++; for (int s = 6; o < ins + L; s -= 2) *o++ = c >> s & 3; }
      else while (o < ins + L) *o++ = f & 4 ? rv(6) : rd4(7);
    }
    spv_w[m] = w; spv_n[m] = (nb + 5) * 4;
  }
}
#define SM(a) shmod(spv_w[a], spv_n[a])
/* mode 0 = fullscreen (no depth), 1 = scene (depth ALWAYS, writes distance), 2 = swarm (depth LESS, coverage blend, strips) */
static VkPipeline mkgfx(VkShaderModule vs, VkShaderModule fs, VkFormat cf, int mode) {
  VkPipelineShaderStageCreateInfo st[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", 0 },
                                            { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", 0 } };
  VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
  VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, 0, 0,
    mode == 2 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0 };
  VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, 0, 0, 1, 0, 1, 0 };
  VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, 0, 0, 0, 0, VK_POLYGON_MODE_FILL,
    VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0, 0, 0, 0, 1.0f };
  VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, 0, 0, VK_SAMPLE_COUNT_1_BIT };
  VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, 0, 0, mode > 0, mode > 0,
    mode == 2 ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS };
  VkPipelineColorBlendAttachmentState ba = { mode == 2, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, 15 };
  VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, 0, 0, 0, VK_LOGIC_OP_CLEAR, 1, &ba };
  VkDynamicState dy[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dsi = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, 0, 0, 2, dy };
  VkPipelineRenderingCreateInfo ri = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, 0, 0, 1, &cf, mode ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED };
  VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri, 0, 2, st, &vi, &ia, 0, &vp, &rs, &ms, &ds, &cb, &dsi, playout, 0, 0, 0, -1 };
  VkPipeline p; CK(vkCreateGraphicsPipelines(dev, 0, 1, &ci, 0, &p)); return p;
}
static VkPipeline mkcomp(VkShaderModule cs, const int *sc) {
  static const VkSpecializationMapEntry me[4] = { { 0, 0, 4 }, { 1, 4, 4 }, { 2, 8, 4 }, { 3, 12, 4 } };
  VkSpecializationInfo si = { 4, me, 16, sc };
  VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0,
    { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs, "main", &si }, playout, 0, -1 };
  VkPipeline p; CK(vkCreateComputePipelines(dev, 0, 1, &ci, 0, &p)); return p;
}
static void begin_render(VkImageView cv, VkImageView dv, int w, int h, int clear) {
  VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 0, cv, L_ATT, 0, 0, L_UND,
    clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE };
  VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 0, dv, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0, 0, L_UND,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE };
  VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, 0, 0, { { 0, 0 }, { (uint32_t)w, (uint32_t)h } }, 1, 0, 1, &ca, dv ? &da : 0, 0 };
  vkCmdBeginRendering(cmd, &ri);
  VkViewport vp = { 0, 0, (float)w, (float)h, 0, 1 }; VkRect2D sc = { { 0, 0 }, { (uint32_t)w, (uint32_t)h } };
  vkCmdSetViewport(cmd, 0, 1, &vp); vkCmdSetScissor(cmd, 0, 1, &sc);
}
static float pv[4];   /* previous frame time, jitter x/y, history valid */
static void push(float a, float b, float c, float d, int k) {
  if (k >= NSYNC) k = NSYNC - 1;
  if (k < 0) k = 0;
  float p[16] = { a, b, c, d };
  for (int i = 0; i < 8; i++) p[4 + i] = syncv[i][k];
  memcpy(p + 12, pv, 16);
  vkCmdPushConstants(cmd, playout, VK_SHADER_STAGE_ALL, 0, 64, p);
}
static void dispatch(VkPipeline p, float a, float b, float c, float d, uint32_t x, uint32_t y, uint32_t z) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p); push(a, b, c, d, 0); vkCmdDispatch(cmd, x, y, z); mbar();
}
/* (re)build an acceleration structure; storage is created on first use */
static void build_as(Acc *A, int top, VkAccelerationStructureGeometryKHR *g, uint32_t np, VkBuildAccelerationStructureFlagsKHR fl, int upd) {
  VkAccelerationStructureTypeKHR ty = top ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR bi = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, 0, ty, fl,
    upd ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, 0, 0, 1, g };
  if (!A->h) {
    VkAccelerationStructureBuildSizesInfoKHR sz = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &np, &sz);
    A->b = mkbuf(sz.accelerationStructureSize, 0);
    VkAccelerationStructureCreateInfoKHR ci = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, 0, 0, A->b.b, 0, sz.accelerationStructureSize, ty };
    CK(vkCreateAccelerationStructureKHR(dev, &ci, 0, &A->h));
    VkAccelerationStructureDeviceAddressInfoKHR ai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, 0, A->h };
    A->a = vkGetAccelerationStructureDeviceAddressKHR(dev, &ai);
    VkDeviceSize need = (sz.buildScratchSize > sz.updateScratchSize ? sz.buildScratchSize : sz.updateScratchSize) + 256;
    if (need > scratch.n) { if (scratch.b) { csubmit(0, 0); cwait(); freebuf(&scratch); cbegin(); } scratch = mkbuf(need, 0); }
  }
  bi.srcAccelerationStructure = upd ? A->h : 0; bi.dstAccelerationStructure = A->h;
  bi.scratchData.deviceAddress = (scratch.a + 255) & ~(VkDeviceAddress)255;
  VkAccelerationStructureBuildRangeInfoKHR r = { np, 0, 0, 0 }; const VkAccelerationStructureBuildRangeInfoKHR *pr = &r;
  vkCmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &pr);
  mbar();
}
static VkAccelerationStructureGeometryKHR geom(VkDeviceAddress a, uint32_t nv, uint32_t stride) {
  VkAccelerationStructureGeometryKHR g; memset(&g, 0, sizeof g);
  g.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR; g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  if (nv) {
    g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    g.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    g.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; g.geometry.triangles.vertexData.deviceAddress = a;
    g.geometry.triangles.vertexStride = stride; g.geometry.triangles.maxVertex = nv - 1; g.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
  } else {
    g.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    g.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    g.geometry.instances.data.deviceAddress = a;
  }
  return g;
}
static void aset(VkAccelerationStructureInstanceKHR *I, const float *m, uint32_t custom, uint32_t mask, VkDeviceAddress ref) {
  memcpy(I->transform.matrix, m, 48); I->instanceCustomIndex = custom; I->mask = mask;
  I->instanceShaderBindingTableRecordOffset = 0; I->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  I->accelerationStructureReference = ref;
}

/* --- procedural meshes for the BVHs ---------------------------------------- */
static float *gv;
static void v3(float x, float y, float z) { *gv++ = x; *gv++ = y; *gv++ = z; }
static void quad(const float *a, const float *b, const float *c, const float *d) {
  v3(a[0], a[1], a[2]); v3(b[0], b[1], b[2]); v3(c[0], c[1], c[2]);
  v3(a[0], a[1], a[2]); v3(c[0], c[1], c[2]); v3(d[0], d[1], d[2]);
}
/* unit habitat band: radius 1, height -1..1, 256 segments */
static int mesh_ring(void) {
  float *s = gv;
  for (int k = 0; k < 256; k++) {
    float a0 = k * 0.0245437f, a1 = a0 + 0.0245437f;
    float p[4][3] = { { cosf(a0), -1, sinf(a0) }, { cosf(a1), -1, sinf(a1) }, { cosf(a1), 1, sinf(a1) }, { cosf(a0), 1, sinf(a0) } };
    quad(p[0], p[1], p[2], p[3]);
  }
  return (int)(gv - s) / 3;
}
static unsigned hu(unsigned x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static float h2(int x, int y) { return (float)hu((unsigned)x * 1973u + hu((unsigned)y + 9277u)) * (1.0f / 4294967295.0f); }
/* smooth value noise, identical to vn2() in common.glsl (tower clustering) */
static float vn2(float x, float y) {
  int i = (int)floorf(x), j = (int)floorf(y); float fx = x - i, fy = y - j;
  fx *= fx * (3 - 2 * fx); fy *= fy * (3 - 2 * fy);
  float a = h2(i, j) + (h2(i + 1, j) - h2(i, j)) * fx, b = h2(i, j + 1) + (h2(i + 1, j + 1) - h2(i, j + 1)) * fx;
  return a + (b - a) * fy;
}
/* habitat ring frame, identical to rframe() in scene.frag; rows of a 3x4 transform scaled by (R, hh, R) */
static void ring_xf(float *m, float ax, float az, float R, float hh) {
  float y[3] = { sinf(az), cosf(az) * cosf(ax), cosf(az) * sinf(ax) }, l = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
  for (int i = 0; i < 3; i++) y[i] /= l;
  float x[3] = { y[1], -y[0], 0 }; l = sqrtf(x[0] * x[0] + x[1] * x[1]); x[0] /= l; x[1] /= l;
  float z[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
  for (int r = 0; r < 3; r++) { m[r * 4] = x[r] * R; m[r * 4 + 1] = y[r] * hh; m[r * 4 + 2] = z[r] * R; m[r * 4 + 3] = 0; }
}
/* --- device, swapchain ----------------------------------------------------- */
static int hasext(VkExtensionProperties *e, uint32_t n, const char *s) { for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, s)) return 1; return 0; }
static void vk_init(void) {
#ifdef _WIN32
  if (!(gipa = (PFN_vkGetInstanceProcAddr)vk_lib())) fatal("Vulkan loader not found (vulkan-1.dll).");
#else
  if (!(gipa = (PFN_vkGetInstanceProcAddr)vk_lib())) fatal("Vulkan loader not found (libvulkan.so.1).");
#endif
  PFN_vkCreateInstance ci_ = (PFN_vkCreateInstance)gipa(0, "vkCreateInstance");
  VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "CATHEDRA", VK_MAKE_VERSION(1, 0, 0), "CATHEDRA", VK_MAKE_VERSION(1, 0, 0), VK_API_VERSION_1_3 };
  const char *ie[2] = { "VK_KHR_surface", SURF_EXT };
  VkInstanceCreateInfo ii = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai, 0, 0, headless ? 0u : 2u, ie };
  CK(ci_(&ii, 0, &vki));
  { const char *n = vk_names; for (unsigned i = 0; i < sizeof vkf / sizeof(void *); i++, n += strlen(n) + 1) ((PFN_vkVoidFunction *)&vkf)[i] = gipa(vki, n); }
  if (!headless) CK(mksurface(vki, &surf));
  VkPhysicalDevice pd[16]; uint32_t np = 16, best = 0; int bs = -1;
  CK(vkEnumeratePhysicalDevices(vki, &np, pd));
  VkExtensionProperties *ex = (VkExtensionProperties *)malloc(1024 * sizeof *ex);
  for (uint32_t i = 0; i < np; i++) {
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i], &p);
    uint32_t ne = 1024; vkEnumerateDeviceExtensionProperties(pd[i], 0, &ne, ex);
    if (p.apiVersion < VK_API_VERSION_1_3 || !hasext(ex, ne, "VK_KHR_ray_query") || !hasext(ex, ne, "VK_KHR_acceleration_structure")) continue;
    VkQueueFamilyProperties qf[16]; uint32_t nq = 16; vkGetPhysicalDeviceQueueFamilyProperties(pd[i], &nq, qf);
    for (uint32_t q = 0; q < nq; q++) {
      VkBool32 pr = 1;
      if (surf) vkGetPhysicalDeviceSurfaceSupportKHR(pd[i], q, surf, &pr);
      int sc = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
      if ((qf[q].queueFlags & 3) == 3 && pr && sc > bs) {
        bs = sc; best = i; qfam = q;
        has_rtp = hasext(ex, ne, "VK_KHR_ray_tracing_pipeline"); has_mesh = hasext(ex, ne, "VK_EXT_mesh_shader");
      }
    }
  }
  free(ex);
  if (bs < 0) fatal("No Vulkan 1.3 GPU with VK_KHR_ray_query + VK_KHR_acceleration_structure found.");
  gpu = pd[best];
  vkGetPhysicalDeviceProperties(gpu, &gprops); vkGetPhysicalDeviceMemoryProperties(gpu, &mprops);
  float pri = 1;
  VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, qfam, 1, &pri };
  const char *de[4] = { "VK_KHR_acceleration_structure", "VK_KHR_ray_query", "VK_KHR_deferred_host_operations", "VK_KHR_swapchain" };
  VkPhysicalDeviceRayQueryFeaturesKHR rq = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, 0, 1 };
  VkPhysicalDeviceAccelerationStructureFeaturesKHR af = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rq, 1 };
  VkPhysicalDeviceVulkan12Features f12; memset(&f12, 0, sizeof f12);
  f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES; f12.pNext = &af; f12.bufferDeviceAddress = 1;
  VkPhysicalDeviceVulkan13Features f13; memset(&f13, 0, sizeof f13);
  f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; f13.pNext = &f12; f13.dynamicRendering = 1; f13.synchronization2 = 1;
  VkPhysicalDeviceFeatures2 f2; memset(&f2, 0, sizeof f2); f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; f2.pNext = &f13;
  f2.features.shaderStorageImageExtendedFormats = 1;
  VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qi, 0, 0, headless ? 3u : 4u, de };
  CK(vkCreateDevice(gpu, &di, 0, &dev));
  vkGetDeviceQueue(dev, qfam, 0, &queue);
  VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfam };
  VkCommandPool pool; CK(vkCreateCommandPool(dev, &pci, 0, &pool));
  VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
  CK(vkAllocateCommandBuffers(dev, &cai, &cmd));
  VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
  CK(vkCreateFence(dev, &fci, 0, &fence));
  VkSemaphoreCreateInfo sci_ = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  for (int i = 0; i < 9; i++) CK(vkCreateSemaphore(dev, &sci_, 0, i ? &sem_done[i - 1] : &sem_acq));
  VkQueryPoolCreateInfo qpi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, 0, 0, VK_QUERY_TYPE_TIMESTAMP, 4 };
  CK(vkCreateQueryPool(dev, &qpi, 0, &qpool));
}
static void mkswap(void) {
  vkDeviceWaitIdle(dev);
  VkSurfaceCapabilitiesKHR c; CK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surf, &c));
  if (c.currentExtent.width != 0xFFFFFFFFu) { W = c.currentExtent.width; H = c.currentExtent.height; }
  resized = 0;
  if (W < 8 || H < 8) return;
  if (!swc) {
    VkSurfaceFormatKHR sf[64]; uint32_t n = 64; vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &n, sf);
    out_fmt = sf[0].format;
    for (uint32_t i = 0; i < n; i++) if (sf[i].format == VK_FORMAT_B8G8R8A8_UNORM || sf[i].format == VK_FORMAT_R8G8B8A8_UNORM) { out_fmt = sf[i].format; break; }
  }
  uint32_t mi = c.minImageCount + 1; if (c.maxImageCount && mi > c.maxImageCount) mi = c.maxImageCount;
  VkSwapchainKHR old = swc;
  VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, 0, 0, surf, mi, out_fmt, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    { (uint32_t)W, (uint32_t)H }, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0,
    c.currentTransform, (VkCompositeAlphaFlagBitsKHR)(c.supportedCompositeAlpha & -c.supportedCompositeAlpha), VK_PRESENT_MODE_FIFO_KHR, 1, old };
  CK(vkCreateSwapchainKHR(dev, &ci, 0, &swc));
  for (uint32_t i = 0; i < nsc; i++) vkDestroyImageView(dev, scv[i], 0);
  if (old) vkDestroySwapchainKHR(dev, old, 0);
  nsc = 8; CK(vkGetSwapchainImagesKHR(dev, swc, &nsc, sci));
  for (uint32_t i = 0; i < nsc; i++) {
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 0, 0, sci[i], VK_IMAGE_VIEW_TYPE_2D, out_fmt, { 0, 0, 0, 0 }, { COL, 0, 1, 0, 1 } };
    CK(vkCreateImageView(dev, &vi, 0, &scv[i]));
  }
}
static uint32_t acquire(void) {
  uint32_t i; VkResult r = vkAcquireNextImageKHR(dev, swc, ~0ull, sem_acq, 0, &i);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { resized = 1; return ~0u; }
  CK(r); return i;
}
static void present(uint32_t i) {
  VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, 0, 1, &sem_done[i], 1, &swc, &i, 0 };
  VkResult r = vkQueuePresentKHR(queue, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) resized = 1; else CK(r);
}
/* thin gold progress bar on black while the precalc runs */
static void loadbar(float f) {
  if (headless) return;
  pump(); if (quit_req) exit(0);
  if (resized) mkswap();
  if (W < 8 || H < 8) return;
  uint32_t i = acquire(); if (i == ~0u) return;
  cbegin(); ibar(sci[i], L_UND, L_ATT, 0, 1, COL); begin_render(scv[i], 0, W, H, 1);
  /* hairline track, gold fill with a soft halo and a bright leading spark */
  int bw = W * 3 / 10, bh = H / 540 + 1, x0 = (W - bw) / 2, y0 = H / 2, fwid = (int)(bw * f) + 1, g = bh * 3;
  if (fwid > bw) fwid = bw;
  VkClearAttachment ca[4] = { { COL, 0, { { { .07f, .06f, .05f, 1 } } } }, { COL, 0, { { { .22f, .15f, .07f, 1 } } } },
    { COL, 0, { { { 1, .78f, .45f, 1 } } } }, { COL, 0, { { { 1, .97f, .9f, 1 } } } } };
  VkClearRect cr[4] = { { { { x0, y0 }, { (uint32_t)bw, (uint32_t)bh } }, 0, 1 }, { { { x0, y0 - g }, { (uint32_t)fwid, (uint32_t)(bh + 2 * g) } }, 0, 1 },
    { { { x0, y0 }, { (uint32_t)fwid, (uint32_t)bh } }, 0, 1 }, { { { x0 + fwid - g, y0 - bh }, { (uint32_t)g, (uint32_t)(bh * 3) } }, 0, 1 } };
  for (int k = 0; k < 4; k++) vkCmdClearAttachments(cmd, 1, ca + k, 1, cr + k);
  vkCmdEndRendering(cmd); ibar(sci[i], L_ATT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 1, COL);
  csubmit(sem_acq, sem_done[i]); present(i); cwait();
}
static Img N, Wv, X, CL, PV, PM, GI;
static Buf Dsw, V, IB, GV, UVB, PRB;
static void alloc_target(int w, int h) {
  if (C.i) { vkDeviceWaitIdle(dev); freeimg(&C); freeimg(&Z); freeimg(&Hh[0]); freeimg(&Hh[1]); }
  fw = w; fh = h; cmips = 1; while ((w | h) >> cmips) cmips++;
  C = mkimg(0, RGBA16, w, h, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  Z = mkimg(0, VK_FORMAT_D32_SFLOAT, w, h, 1, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  for (int i = 0; i < 2; i++) Hh[i] = mkimg(0, RGBA16, w, h, 1, cmips, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  cbegin(); for (int i = 0; i < 2; i++) ibar(Hh[i].i, L_UND, L_RO, 0, cmips, COL); csubmit(0, 0); cwait();
  set_img(18, CIS, smp_clamp, C.v, L_RO);
  pv[3] = 0;
}
/* one frame: [finale: debris + swarm BLAS refit + TLAS rebuild] -> ray-queried/marched scene
 * -> instanced swarm -> temporal AA (history ping-pong) -> mip chain -> post into the target */
static void record_frame(float t, float scale, VkImage ti, VkImageView tv, VkImageLayout fin) {
  int k = (int)(t * SYNC_HZ), prv = hcur; hcur ^= 1;
  set_img(19, CIS, smp_clamp, Hh[prv].v0, L_RO);
  set_img(2, CIS, smp_clamp, Hh[hcur].v, L_RO);
  cbegin();
  vkCmdResetQueryPool(cmd, qpool, 0, 4);
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qpool, 0);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, 0);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &dset, 0, 0);
  dispatch(pip_cam, t, pv[0], 0, 0, 1, 1, 1);
  /* the finale's per-frame BVH work (swarm refit + TLAS rebuild): only the finale
     traces TB. (It used to start at 112 s to keep the GPU clock up; the supersampled
     orbit keeps it busy now, and the probe gives the orbit the 1.6 ms instead) */
  if (t >= 144) {
    dispatch(pip_chunks, t, (float)nch, 0, 0, (nch + 63) / 64, 1, 1);
    dispatch(pip_rtv, t, 0, (float)fh, (float)fw, npanels / 256, 1, 1);
    build_as(&tlasB, 1, &g_tb, ntb, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, 0);
  }
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpool, 1);
  ibar(C.i, L_UND, L_ATT, 0, 1, COL); ibar(Z.i, L_UND, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0, 1, VK_IMAGE_ASPECT_DEPTH_BIT);
  begin_render(C.v0, Z.v, fw, fh, 0);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pip_scene); push(t, (float)fw, (float)fh, scale, k);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  if (t >= 144) { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pip_swarm); push(t, (float)fw, (float)fh, scale, k); vkCmdDraw(cmd, 4, npanels, 0, 0); }
  vkCmdEndRendering(cmd);
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpool, 2);
  ibar(C.i, L_ATT, L_RO, 0, 1, COL);
  ibar(Hh[hcur].i, L_RO, L_ATT, 0, 1, COL);
  begin_render(Hh[hcur].v0, 0, fw, fh, 0);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pip_taa); push(t, (float)fw, (float)fh, scale, k);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRendering(cmd);
  mips(Hh[hcur].i, fw, fh, 1, cmips, L_ATT, L_RO);
  ibar(ti, L_UND, L_ATT, 0, 1, COL);
  begin_render(tv, 0, W, H, 0);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pip_post); push(t, (float)W, (float)H, (float)fw / W, k);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRendering(cmd);
  ibar(ti, L_ATT, fin, 0, 1, COL);
  vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpool, 3);
}
static int fcount;
/* TAA bookkeeping for the next frame: Halton(2,3) sub-pixel jitter, history validity */
static void next_frame(float t) {
  int n = (fcount++ & 7) + 1; float hx = 0, hy = 0, f = .5f;
  for (int i = n; i; i >>= 1, f *= .5f) hx += f * (i & 1);
  f = 1.f / 3; for (int i = n; i; i /= 3, f /= 3) hy += f * (i % 3);
  /* history: 0 = restart (a cut); else 1 + w, w the least weight of the new frame: about 1/(n+1) in the n-th frame after
   * a cut, so the first frames average evenly instead of one raw, noisy frame fading out slowly */
  pv[3] = fcount > 1 && (int)(t / 16) == (int)(pv[0] / 16) && fabsf(t - pv[0]) < .3f ? (pv[3] > .5f ? 1 + (pv[3] - 1) / pv[3] : 1.45f) : 0.f;
  pv[1] = hx - .5f; pv[2] = hy - .5f;
}
static void frame_done(float t) { pv[0] = t; }
static float gpu_ms[3];   /* AS build, scene, TAA+mips+post of the last finished frame */
static void read_ts(void) {
  unsigned long long r[4];
  if (vkGetQueryPoolResults(dev, qpool, 0, 4, sizeof r, r, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
  for (int i = 0; i < 3; i++) gpu_ms[i] = (float)((r[i + 1] - r[i]) * gprops.limits.timestampPeriod * 1e-6);
}

/* supersampling: one render scale per section, measured before the show starts (probe), applied
 * only at the section cut, where the TAA history restarts anyway - no resolution switch (= history
 * reset + resample) can ever happen in mid-shot. Inside a shot only an emergency step down (> 15 ms). */
static float gpu_acc, sscale[NSEC]; static int nacc, asec = -1;
static float adapt(float scale, float t) {
  int s = (int)(t / 16); if (s >= NSEC) s = NSEC - 1;
  if (s != asec) { asec = s; nacc = 0; gpu_acc = 0; return sscale[s]; }
  gpu_acc += gpu_ms[0] + gpu_ms[1] + gpu_ms[2];
  if (++nacc >= 30) { if (gpu_acc / nacc > 15 && scale > .5f) sscale[s] = scale -= .125f; nacc = 0; gpu_acc = 0; }
  return scale;
}
/* 4 instants per section at 1x (2 warm frames + 1 timed each, after a GPU clock warm-up), into an
 * offscreen target; scale = sqrt((12 ms - BVH) / worst rest) in 1/8 steps, 0.5x-2x: the finale's
 * per-frame BVH work does not grow with the resolution, the scene and post do */
static void probe(VkImage ti, VkImageView tv) {
  alloc_target(W, H);
  for (int s = -1; s < NSEC; s++) {
    float w = 0, b = 0;
    for (int k = 0; k < (s < 0 ? 30 : 12); k++) {
      float t = (s < 0 ? 8 : s * 16 + 1.5f + (k / 3) * 4.2f) + (k % 3) / 60.f;
      next_frame(t); record_frame(t, 1, ti, tv, VK_IMAGE_LAYOUT_GENERAL); csubmit(0, 0); cwait(); read_ts(); frame_done(t);
      float ms = gpu_ms[1] + gpu_ms[2];
      if (k % 3 == 2) { if (ms > w) w = ms; if (gpu_ms[0] > b) b = gpu_ms[0]; }
    }
    if (s >= 0) { float c = floorf(sqrtf((12.f - b) / (w + .01f)) * 8) / 8; sscale[s] = c < .5f ? .5f : c > 2 ? 2 : c; }
  }
}

/* --- precalc ---------------------------------------------------------------- */
static void stage(float f) { csubmit(0, 0); cwait(); loadbar(f); cbegin(); vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, 0); }
#define SAMPST (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
static int big_alloc(int t) {
  const int *q = tiers[t]; int pm = 1; while (q[4] >> pm) pm++;
  soft = 1; afail = 0;
  CL = mkimg(1, RGBA16, q[0], q[1], q[2], 1, SAMPST); GI = mkimg(1, RGBA16, q[0] / 4, q[1] / 2 + 8, q[2] / 4, 1, SAMPST);
  PV = mkimg(1, RGBA16, q[3], q[3], q[3], 1, SAMPST);
  PM = mkimg(0, RGBA16, q[4], q[4] / 2, 1, pm - 1, SAMPST);
  Dsw = mkbuf((VkDeviceSize)q[6] * 32 + 64, 0); V = mkbuf((VkDeviceSize)q[6] * 48, 0);
  soft = 0;
  if (afail) { freeimg(&GI); freeimg(&CL); freeimg(&PV); freeimg(&PM); freebuf(&Dsw); freebuf(&V); return 0; }
  npanels = q[6];
  nch = q[7];
  return 1;
}
#ifdef CATHEDRA_DEV
static double tst[10]; static int ntst;
#define TSTAMP() (tst[ntst++] = now_sec())
#else
#define TSTAMP()
#endif
static Acc blas_v[48], blas_rock[8], blas_sc, blas_br;
static int ntow;
static void precalc(void) {
  int pmw = tiers[tier][4], pmh = pmw / 2, pvn = tiers[tier][3], nv = tiers[tier][5], pmm = 1; while (pmw >> pmm) pmm++;
  const int *cq = tiers[tier];
  int xm = 12;
  N = mkimg(1, VK_FORMAT_R8_UNORM, 64, 64, 64, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  Wv = mkimg(1, VK_FORMAT_R8G8B8A8_UNORM, 128, 128, 128, 8, SAMPST);
  X = mkimg(0, VK_FORMAT_R8_UNORM, TXW, TXH, 1, xm, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  Buf stage_ = mkbuf(64 * 64 * 64 + TXW * TXH, 1);
  IB = mkbuf(64 * (4 + nch), 1);
  { unsigned s = 0x9E3779B9u;   /* 64^3 value noise */
    for (int i = 0; i < 64 * 64 * 64; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; stage_.p[i] = (unsigned char)(s >> 24); } }
  build_text(stage_.p + 64 * 64 * 64);
  set_img(0, CIS, smp_rep, N.v, L_RO);
  set_img(1, CIS, smp_clamp, X.v, L_RO);
  set_vol(3, &Wv, smp_rep); set_vol(10, &CL, smp_clamp); set_img(21, CIS, smp_clamp, GI.v, L_GEN); set_img(22, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, GI.v0, L_GEN); set_vol(14, &PV, smp_clamp); set_vol(16, &PM, smp_rep);
  set_buf(6, &Dsw); set_buf(7, &V); set_buf(20, &IB);
  /* architecture grammar -> parts (CPU, a few ms) */
  PRT = (float *)malloc(sizeof(float) * 16 * 600000);
  int vb[48], vn[48], rb[8], sb, sn, bb; float vh[48];
  for (int j = 0; j < nv; j++) { vb[j] = ntri; vh[j] = 6 + 36.f * j / (nv - 1); tower_(vh[j], j * 7 + 3, ((j * 5 + 1) % 3 == 0) | (j % 6 == 1) * 2); vn[j] = ntri - vb[j]; }
  for (int k = 0; k < 8; k++) { rb[k] = ntri; part(5, M_ROCK, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, (float)(k * 31 + 5), 0); }
  sb = ntri; scaffold_(); sn = ntri - sb;
  /* sky bridge (unit span along x, metres across/up): arched deck + two parapets */
  bb = ntri; part(1, M_STONE, 0, 0, 0, 1, 0, 0, 2.2f, 1.4f, 6, 0, 0, 0);
  for (int k = -1; k <= 1; k += 2) part(1, M_TRIM, 0, 1.1f, k * 2.05f, 1, 1.1f, k * 2.05f, .16f, 1.1f, 0, 0, 0, 0);
  PRB = mkbuf((VkDeviceSize)npart * 64, 1); memcpy(PRB.p, PRT, (size_t)npart * 64); free(PRT);
  GV = mkbuf((VkDeviceSize)ntri * 48, 0); UVB = mkbuf((VkDeviceSize)ntri * 24, 0);
  set_buf(12, &GV); set_buf(5, &UVB); set_buf(13, &PRB);
  /* --- GPU precalc --- */
  cbegin();
  ibar(N.i, L_UND, L_DST, 0, 1, COL);
  VkBufferImageCopy bc = { 0, 0, 0, { COL, 0, 0, 1 }, { 0, 0, 0 }, { 64, 64, 64 } };
  vkCmdCopyBufferToImage(cmd, stage_.b, N.i, L_DST, 1, &bc);
  ibar(N.i, L_DST, L_RO, 0, 1, COL);
  ibar(X.i, L_UND, L_DST, 0, 1, COL);
  VkBufferImageCopy bx = { 64 * 64 * 64, 0, 0, { COL, 0, 0, 1 }, { 0, 0, 0 }, { TXW, TXH, 1 } };
  vkCmdCopyBufferToImage(cmd, stage_.b, X.i, L_DST, 1, &bx);
  mips(X.i, TXW, TXH, 1, xm, L_DST, L_RO);
  ibar(Wv.i, L_UND, L_GEN, 0, 8, COL); ibar(CL.i, L_UND, L_GEN, 0, 1, COL); ibar(PV.i, L_UND, L_GEN, 0, 1, COL); ibar(PM.i, L_UND, L_GEN, 0, pmm - 1, COL);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, 0);
  dispatch(pip_noise, 0, 0, 0, 0, 16, 16, 16);
  mips(Wv.i, 128, 128, 128, 8, L_GEN, L_GEN);
  dispatch(pip_bswarm, 0, 0, 0, 0, npanels / 256, 1, 1);
  dispatch(pip_mesh, 0, (float)npart, 0, 0, (npart + 63) / 64, 1, 1);
  stage(.3f); TSTAMP();
  /* planet world map (orbit section) */
  for (int r = 0; r < pmh; r += pmh / 4) { dispatch(pip_pmap, (float)r, 0, 0, 0, pmw / 8, pmh / 32, 1); stage(.3f + .05f * r / pmh); }
  mips(PM.i, pmw, pmh, 1, pmm - 1, L_GEN, L_GEN);
  TSTAMP();
  /* planet SDF / heat / strata volume, in slabs so the bar moves */
  for (int z = 0; z < pvn; z += 32) { dispatch(pip_planet, (float)z, 0, 0, 0, pvn / 4, pvn / 4, 8); stage(.35f + .25f * z / pvn); }
  TSTAMP();
  /* bottom-level acceleration structures: tower variants, rocks, scaffold, ring */
  VkBuildAccelerationStructureFlagsKHR ft = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  VkAccelerationStructureGeometryKHR g;
  for (int j = 0; j < nv; j++) { g = geom(GV.a + (VkDeviceSize)vb[j] * 48, vn[j] * 3, 16); build_as(&blas_v[j], 0, &g, vn[j], ft, 0); }
  for (int k = 0; k < 8; k++) { g = geom(GV.a + (VkDeviceSize)rb[k] * 48, 960, 16); build_as(&blas_rock[k], 0, &g, 320, ft, 0); }
  g = geom(GV.a + (VkDeviceSize)sb * 48, sn * 3, 16); build_as(&blas_sc, 0, &g, sn, ft, 0);
  g = geom(GV.a + (VkDeviceSize)bb * 48, 720, 16); build_as(&blas_br, 0, &g, 240, ft, 0);
  Buf geo = mkbuf(256 * 6 * 12, 1); gv = (float *)geo.p;
  int nring = mesh_ring();
  g = geom(geo.a, nring, 12); build_as(&blas_ring, 0, &g, nring / 3, ft, 0);
  stage(.85f);
  /* top level A: the spire city */
  Buf iA = mkbuf(64 * 3600, 1);
  VkAccelerationStructureInstanceKHR *ia = (VkAccelerationStructureInstanceKHR *)iA.p;
  float lx = 0, lz = 0; int lc = 99;   /* previous tower of this row (sky bridges) */
  for (int cy = -99; cy <= -7; cy++) for (int cx = -20; cx <= 19; cx++) {   /* same rule as pcell(); 6 km deep, past the 4 km view */
    int hero = (cx == 1 && cy == -12) || (cx == 1 && cy == -7) || (cx == -2 && cy == -8);
    float ax = fabsf(cx + .5f), cl = vn2(cx * .23f, cy * .23f), px, pz, Ht, R, y0 = 0;
    if (hero) {
      px = (cx + .5f) * 60 + (h2(cx + 7, cy + 1) - .5f) * 10; pz = (cy + .5f) * 60;
      Ht = 100 + h2(cx + 3, cy + 9) * 170; R = 8 + 5 * h2(cx + 5, cy + 2);
    } else {
      if (!(h2(cx, cy) < .2f + .65f * cl && (ax > 1 || cy < -59) && ax < 20)) continue;   /* the avenue ends at 3.6 km: no vista to an empty horizon */
      px = (cx + .5f) * 60 + (h2(cx + 7, cy + 1) - .5f) * 34; pz = (cy + .5f) * 60 + (h2(cx + 13, cy + 4) - .5f) * 34;
      Ht = 110 + 250 * powf(h2(cx + 3, cy + 9), 1.5f) * (.55f + .7f * cl); R = 7 + 6 * h2(cx + 5, cy + 2);
      y0 = -12 - 40 * h2(cx + 9, cy + 6); Ht -= y0;   /* footings sunk deep in the cloud sea */
    }
    float hb = Ht / R, a = h2(cx + 11, cy + 3) * 6.2832f;
    int j = (int)((hb - 6) / 36.f * (nv - 1) + .5f + (hero ? 0 : h2(cx + 19, cy + 2) * 5 - 2.5f)); j = j < 0 ? 0 : j >= nv ? nv - 1 : j;
    if (hero && j % 6 == 1) j++;   /* neighbouring variants mixed in (no clones); heroes are never ruins */
    if (!hero && lc == cx - 1 && h2(cx, cy + 29) < .35f) {   /* sky bridge to the western neighbour (never over the avenue, never at a hero) */
      float m[12] = { px - lx, 0, 0, lx, 0, 1, 0, 70 + 40 * h2(cx + 5, cy), pz - lz, 0, 1, lz };   /* below every body top (>= 110 m) */
      aset(ia + ntow++, m, bb, 1, blas_br.a);
    }
    lx = px; lz = pz; lc = hero ? 99 : cx;
    float ys = hb / vh[j], c = cosf(a) * R, s = sinf(a) * R;
    float m[12] = { c, 0, s, px, 0, R * ys, 0, y0, -s, 0, c, pz };
    aset(ia + ntow++, m, vb[j], 1, blas_v[j].a);
  }
  g = geom(iA.a, 0, 0); build_as(&tlasA, 1, &g, ntow, ft, 0);
  /* top level B (finale): swarm + two ring habitats + rim scaffolds + debris chunks */
  { float m[12];
    VkAccelerationStructureInstanceKHR *ib = (VkAccelerationStructureInstanceKHR *)IB.p;
    float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }, sp[12] = { 1, 0, 0, 6.2f, 0, 1, 0, .5f, 0, 0, 1, 3.4f };
    aset(ib, id, 0, 0, blas_ring.a);   /* slot 0: unused (mask 0) */
    ring_xf(m, .32f, .1f, 5.2f, .1f); aset(ib + 1, m, 1, 2, blas_ring.a);
    ring_xf(m, -.45f, -.2f, 7.6f, .15f); aset(ib + 2, m, 2, 2, blas_ring.a);
    aset(ib + 3, sp, sb, 4, blas_sc.a);
    for (int i = 0; i < nch; i++) aset(ib + 4 + i, id, rb[i & 7], 8, blas_rock[i & 7].a);
    ntb = 4 + nch;
    dispatch(pip_chunks, 144, (float)nch, 0, 0, (nch + 63) / 64, 1, 1);
    g_tb = geom(IB.a, 0, 0);
    build_as(&tlasB, 1, &g_tb, ntb, ft, 0);
  }
  csubmit(0, 0); cwait();
  set_as(8, &tlasA); set_as(9, &tlasB);
  TSTAMP();
  /* cloud sea (needs the city BVH): density, then lighting - sun and sky optical depth through
     the baked density and 1-96 ray-traced tower-shadow rays per voxel (the heavy part; tier 3 = 96) */
  cbegin(); vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0, 0);
  for (int m = 0; m < 2; m++) for (int z = 0; z < cq[2]; z += 32) {
    dispatch(pip_cloud, (float)z, (float)((1 << tier) * (tier > 2 ? 12 : 1)), 0, (float)m, cq[0] / 8, cq[1] / 4, 4); stage(.62f + .3f * (m * cq[2] + z) / (2 * cq[2]));
  }
  TSTAMP();   /* (stage() above already waited for the GPU) */
  /* city light volume: 32-3072 rays per voxel (tier 3: 3072) over the sphere + a sun shadow ray per hit */
  for (int z = 0; z < cq[2] / 4; z += 16) {
    dispatch(pip_cloud, (float)z, (float)((32 << tier) * (tier > 2 ? 12 : 1)), 0, 2, cq[0] / 32, (cq[1] / 2 + 8 + 3) / 4, 2); stage(.92f + .06f * z / (cq[2] / 4));
  }
  csubmit(0, 0); cwait();
  TSTAMP();
  loadbar(.98f);
}

#ifdef CATHEDRA_DEV
#include "devtools.h"
#endif

int main(int argc, char **argv) {
  float fixed_scale = 0;
#ifdef CATHEDRA_DEV
  dev_args(argc, argv, &fixed_scale);
#else
  (void)argc; (void)argv;
#endif
  /* music renders on worker threads while we set up Vulkan, build pipelines and bake */
  thr_t sj = thr_go(synth_job, 0);
#ifdef _WIN32
  QueryPerformanceFrequency(&qf);
#endif
  TSTAMP();
  if (!headless) platform_init();
  vk_init();
  if (!headless) mkswap();
  loadbar(0.01f);
  /* descriptors: 0 N, 1 titles, 2 TAA output (post), 3/4 W, 5 mesh UVs, 6 swarm, 7 RT verts, 8/9 TLAS,
   * 10/11 clouds, 12 mesh triangles, 13 parts, 14/15 planet volume, 16/17 planet map, 18 scene HDR, 19 history, 20 TLAS-B instances */
  static const unsigned char btype[23] = { 1, 1, 1, 1, 3, 7, 7, 7, 0, 0, 1, 3, 7, 7, 1, 3, 1, 3, 1, 1, 7, 1, 3 };
  VkDescriptorSetLayoutBinding lb[23];
  for (int b = 0; b < 23; b++) {
    VkDescriptorSetLayoutBinding x = { (uint32_t)b, b == 8 || b == 9 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : (VkDescriptorType)btype[b], 1, VK_SHADER_STAGE_ALL, 0 };
    lb[b] = x;
  }
  VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, 23, lb };
  VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(dev, &dli, 0, &dsl));
  VkDescriptorPoolSize ps[4] = { { CIS, 10 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 }, { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2 } };
  VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 1, 4, ps };
  VkDescriptorPool dp; CK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
  VkDescriptorSetAllocateInfo dai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, dp, 1, &dsl };
  CK(vkAllocateDescriptorSets(dev, &dai, &dset));
  VkPushConstantRange pcr = { VK_SHADER_STAGE_ALL, 0, 64 };
  VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &dsl, 1, &pcr };
  CK(vkCreatePipelineLayout(dev, &pli, 0, &playout));
  /* pipelines (the driver compiles SPIR-V to GPU code here) */
  unspv();
  VkShaderModule vq = SM(spv_quad);
  pip_post = mkgfx(vq, SM(spv_post), out_fmt, 0);
  pip_taa = mkgfx(vq, SM(spv_taa), RGBA16, 0);
  pip_swarm = mkgfx(SM(spv_swarm_vs), SM(spv_swarm_fs), RGBA16, 2);
  { static const unsigned char lsz[9][3] = { { 8, 8, 8 }, { 255, 1, 1 }, { 255, 1, 1 }, { 64, 1, 1 }, { 8, 4, 8 }, { 4, 4, 4 }, { 8, 8, 1 }, { 64, 1, 1 }, { 1, 1, 1 } };
    VkShaderModule bm = SM(spv_bake);
    for (int i = 0; i < 9; i++) { int sc[4] = { i, lsz[i][0] + (lsz[i][0] == 255), lsz[i][1], lsz[i][2] }; pcomp[i] = mkcomp(bm, sc); } }
  loadbar(0.05f);
  pip_scene = mkgfx(vq, SM(spv_scene), RGBA16, 1);
  TSTAMP();
  loadbar(0.12f);
  VkSamplerCreateInfo sci_ = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, 0, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0, 0, 1, 0, VK_COMPARE_OP_NEVER, 0, 1000 };
  CK(vkCreateSampler(dev, &sci_, 0, &smp_rep));
  sci_.addressModeU = sci_.addressModeV = sci_.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  CK(vkCreateSampler(dev, &sci_, 0, &smp_clamp));
  /* quality tier from the GPU type and its VRAM; drop a tier if the big volumes do not fit */
  if (tier < 0) {
    VkDeviceSize vram = 0;
    for (uint32_t i = 0; i < mprops.memoryHeapCount; i++) if ((mprops.memoryHeaps[i].flags & 1) && mprops.memoryHeaps[i].size > vram) vram = mprops.memoryHeaps[i].size;
    tier = gprops.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : vram > (12ull << 30) ? 3 : vram > (7ull << 30) ? 2 : vram > (3ull << 30) ? 1 : 0;
  }
  while (!big_alloc(tier)) if (--tier < 0) fatal("not enough GPU memory");
  precalc();
  while (synth_done < NSEC + 1) { loadbar(0.9f + 0.1f * synth_done / (NSEC + 1)); nap(); }
  thr_join(sj);
  TSTAMP();
  loadbar(1.0f);
#ifdef CATHEDRA_DEV
  if (exp_dir) return dev_export(fixed_scale);
  if (headless) return dev_selftest(fixed_scale);
#endif

  float scale = fixed_scale;
  if (scale <= 0) { Img P = mkimg(0, out_fmt, W, H, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT); probe(P.i, P.v); }
#if defined(CATHEDRA_DEV) && !defined(_WIN32)
  acur = (int)(exp_from * SR); apos = exp_from;   /* --from / --section: the audio starts there too */
#endif
  audio_start();
  t_clock0 = now_sec();
#if defined(CATHEDRA_DEV) && !defined(_WIN32)
  t_clock0 -= exp_from; dev_play_log(-1);
#endif
  double clk_b = t_clock0;
  while (!quit_req) {
    pump();
    if (resized) mkswap();
    if (W < 8 || H < 8) { nap(); continue; }
    /* frame clock: the wall clock, slewed gently toward the audio position (waveOut/ALSA positions
       arrive in coarse, uneven steps - used directly they make every camera move judder) */
    double a = audio_time(), w = now_sec(), t = w - clk_b, e = a - t;
    if (e > .08 || e < -.08 || paused) { clk_b = w - a; t = a; } else clk_b -= e * .03;
#if defined(CATHEDRA_DEV) && !defined(_WIN32)
    if (t >= exp_to) { if (!play_loop) break; dev_seek(exp_from); clk_b = now_sec() - exp_from; continue; }
    dev_play_log(t);
#endif
    if (t >= SONG_SEC) break;
    if (t < 0) t = 0;
    if (fixed_scale <= 0) scale = adapt(scale, (float)t);
    int rw = (int)(W * scale), rh = (int)(H * scale);
    if (rw != fw || rh != fh) alloc_target(rw, rh);
    uint32_t i = acquire(); if (i == ~0u) continue;
    next_frame((float)t);
    record_frame((float)t, scale, sci[i], scv[i], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    csubmit(sem_acq, sem_done[i]); present(i); cwait(); read_ts();
    frame_done((float)t);
  }
  vkDeviceWaitIdle(dev);
  platform_quit();
  return 0;
}

#if !defined(_WIN32) && !defined(CATHEDRA_DEV)
/* Linux release: no C runtime startup (crt1/crti, __libc_start_main). The dynamic loader has already
 * initialised libc (and its environment, which X11, ALSA and the Vulkan loader read); the release
 * takes no arguments. The kernel enters with the stack 16-byte aligned, not as after a call: realign it */
__attribute__((noreturn, force_align_arg_pointer)) void _start(void) { exit(main(0, 0)); }
#endif

#if defined(_WIN32) && defined(TINY_CRT)
/* MinGW size build: skip the MinGW C runtime startup (pseudo-relocs, TLS, atexit tables...).
 * msvcrt.dll initialises itself; the dev build fetches argc/argv, the release build takes none. */
void __main(void) {}
#ifdef CATHEDRA_DEV
typedef struct { int newmode; } startupinfo_t;
extern int __cdecl __getmainargs(int *, char ***, char ***, int, startupinfo_t *);
void __stdcall entry(void) {
  int argc; char **argv, **envp; startupinfo_t si = { 0 };
  __getmainargs(&argc, &argv, &envp, 0, &si);
  ExitProcess((UINT)main(argc, argv));
}
#else
void __stdcall entry(void) { ExitProcess((UINT)main(0, 0)); }
#endif
#endif
