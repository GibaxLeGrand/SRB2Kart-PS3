// i_video_ps3_gcm.c — SRB2Kart PS3 video backend, raw cellGcm (no SDL)
// See i_video_ps3_gcm.h for why this exists.
//
// Pattern adapted from TanakaDOOM-cGcm's i_video_ps3.c (GPLv2,
// github.com/kan8223-dotcom/TanakaDOOM-cGcm), verified to compile and link
// against our PS3DK checkout (-lrsx -lgcm_sys -lsysutil) before use here.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>
#include <sys/systime.h> // sysGetCurrentTime -- real microsecond timing, see note below
#include <cell/gcm/ps3tc_fifo_wrap.h> // ps3tc_fifo_wrap_install -- see PS3GCM_VideoInit

#include "i_video_ps3_gcm.h"
#include "../d_main.h" // PS3_DebugPath
#include "../screen.h" // BASEVIDWIDTH, BASEVIDHEIGHT

// Wall-clock timing for PS3GCM_FinishUpdate, to find out whether the CPU
// palette-convert-and-scale loop is where per-frame time actually goes.
// Uses sysGetCurrentTime() directly rather than I_GetPreciseTime()
// (SDL_GetPerformanceCounter): SDL2_PSL1GHT's timer backend
// (src/timer/psl1ght/SDL_systimer.c) implements SDL_GetPerformanceCounter()
// as SDL_GetTicks() with a frequency of 1000 -- millisecond resolution only,
// which rounded every reading here to 0.00ms and told us nothing.
//
// Deliberately NOT instrumenting PS3GCM_Flip(): the gcmSetFlip()/
// flush_buffer() sequence is exactly the code that used to hang
// deterministically under SDL2_PSL1GHT (see project notes on the flip #555
// bug), and that whole investigation found the failure to be sensitive to
// extra syscalls placed near the RSX submit -- sometimes masking it,
// sometimes not. This backend's Flip is proven stable for 17000+ frames
// specifically because it does nothing but submit and return; keep it that
// way rather than risk reintroducing timing-dependent flakiness for the
// sake of a measurement.
#define PS3GCM_TIMING_FRAMES 30
static unsigned int ps3gcm_timing_calls = 0;

static u64 ps3gcm_now_us(void)
{
	u64 sec = 0, nsec = 0;
	sysGetCurrentTime(&sec, &nsec);
	return sec * 1000000ULL + nsec / 1000ULL;
}

static void ps3gcm_log_us(const char *label, u64 t0_us, u64 t1_us)
{
	FILE *f = fopen(PS3_DebugPath("psdebugL.txt"), "a");
	if (f)
	{
		fprintf(f, "[call#%u] %s: %llu us\n", ps3gcm_timing_calls, label,
			(unsigned long long)(t1_us - t0_us));
		fflush(f);
		fclose(f);
	}
}

// ============================================================
// RSX Context & VRAM Management
// ============================================================

#define CMD_SIZE   (0x10000)    // 64KB command buffer
#define HOST_SIZE  (1024*1024)  // 1MB host memory

#define PS3_SCREEN_W  1280
#define PS3_SCREEN_H   720
#define PS3_FB_COUNT     2

#define SRC_W  BASEVIDWIDTH  // 320
#define SRC_H  BASEVIDHEIGHT // 200

#define SCALE_X  (PS3_SCREEN_W / SRC_W)               // 4
#define SCALE_Y  ((PS3_SCREEN_H - 120) / SRC_H)        // 3 (leaves room for letterbox)
#define DISPLAY_W  (SRC_W * SCALE_X)                   // 1280
#define DISPLAY_H  (SRC_H * SCALE_Y)                   // 600
#define OFFSET_Y   ((PS3_SCREEN_H - DISPLAY_H) / 2)     // 60

static gcmContextData *context = NULL;
static gcmConfiguration rsx_config;
static void *vram_heap = NULL;

static void *fb_addr[PS3_FB_COUNT];
static u32   fb_offset[PS3_FB_COUNT];
static u32   fb_index = 0;

static u32 color_pitch;

static u32 palette_argb[256];
static u32 frame_count = 0;
static int letterbox_cleared = 0;

// ============================================================
// VRAM bump allocator
// ============================================================

static void vram_init(void)
{
	gcmGetConfiguration(&rsx_config);
	vram_heap = rsx_config.localAddress;
}

static void *vram_alloc(u32 alignment, u32 size)
{
	u64 ptr = (u64)vram_heap;
	ptr = (ptr + (alignment - 1)) & ~((u64)alignment - 1);
	if (ptr + size > (u64)rsx_config.localAddress + rsx_config.localSize)
		return NULL;
	vram_heap = (void *)(ptr + size);
	return (void *)ptr;
}

static void flush_buffer(void)
{
	gcmControlRegister volatile *ctrl = gcmGetControlRegister();
	u32 offset = 0;
	__asm __volatile__("sync");
	gcmAddressToOffset((void *)(u64)context->current, &offset);
	ctrl->put = offset;
}

// ============================================================
// Command-FIFO watch -- added 2026-08-26 ~17:00
// ============================================================
//
// Why: we call gcmInitBody() directly, so the command FIFO keeps whatever
// wrap callback the firmware installed. PS3DK deliberately replaces that one:
// <cell/gcm.h>'s cellGcmInit() calls ps3tc_fifo_wrap_install() with the
// comment "We override the firmware default installed by rsxInit /
// gcmInitBodyEx because it doesn't advance PUT in the wrap sequence".
// We never took that path, so we are running on the callback its own
// toolchain calls broken.
//
// Arithmetic that makes this worth a run: the FIFO is CMD_SIZE = 64KB and the
// only thing we ever push into it is one gcmSetFlip per frame. The furthest
// the game has ever got is frame 1795 before RPCS3 host-segfaulted; 65536/1795
// is ~36 bytes per flip, which is exactly the size a flip command sequence
// should be. So the first FIFO wrap and the crash may well be the same event.
//
// This watch is here to decide that by measurement, not by arithmetic. It
// costs a couple of pointer reads per flip and writes a psdebugS.txt line
// only (a) for the first few flips, to establish bytes-per-flip, (b) once
// every 256 flips, (c) every flip once the write pointer is within a few
// flips of the end, and (d) on the flip after a wrap actually happened.
// If the wrap is the killer, the last line in psdebugS.txt will be a
// "danger" line and no "WRAPPED" line will follow it.
//
// Gated by PS3TRACE_FIFO (bit 6) so it can be switched off for timing runs,
// like every other trace family -- see the ps3_debugtrace block in d_main.c.
extern int ps3_debugtrace;
#define PS3TRACE_FIFO 64

static u32 ps3gcm_fifo_prev_cur = 0;
static u32 ps3gcm_fifo_bytes_per_flip = 0;
static u32 ps3gcm_fifo_wraps = 0;
static u32 ps3gcm_fifo_first_cur = 0;

// context->begin/end/current are 32-bit PRX pointers (ATTRIBUTE_PRXPTR), so
// casting them straight to an integer warns about the size change. Go through
// a full-width pointer, the way flush_buffer() already does.
static u32 ps3gcm_ptr32(const void *p)
{
	return (u32)(uintptr_t)p;
}

static void ps3gcm_fifo_line(const char *why, u32 cur)
{
	gcmControlRegister volatile *ctrl = gcmGetControlRegister();
	FILE *f = fopen(PS3_DebugPath("psdebugS.txt"), "a");
	u32 begin = ps3gcm_ptr32(context->begin);
	u32 end = ps3gcm_ptr32(context->end);

	if (!f)
		return;

	fprintf(f, "[FIFO f%-6u] %-14s begin=%08x end=%08x cur=%08x used=%6u free=%6u B/flip=%u put=%08x get=%08x wraps=%u\n",
		(unsigned)frame_count, why, (unsigned)begin, (unsigned)end, (unsigned)cur,
		(unsigned)(cur - begin), (unsigned)(end > cur ? end - cur : 0),
		(unsigned)ps3gcm_fifo_bytes_per_flip,
		(unsigned)ctrl->put, (unsigned)ctrl->get, (unsigned)ps3gcm_fifo_wraps);

	fflush(f);
	fclose(f);
}

// stage 0 = before gcmSetFlip, stage 1 = after gcmSetFlip + flush_buffer.
// Only the danger zone logs both, so that a death inside gcmSetFlip is
// distinguishable from a death after it.
static void ps3gcm_fifo_watch(int stage)
{
	u32 cur, end, danger;

	if (!(ps3_debugtrace & PS3TRACE_FIFO) || !context)
		return;

	cur = ps3gcm_ptr32(context->current);
	end = ps3gcm_ptr32(context->end);

	if (stage == 0)
	{
		if (ps3gcm_fifo_first_cur == 0)
			ps3gcm_fifo_first_cur = cur;
		else if (ps3gcm_fifo_wraps == 0 && frame_count > 0 && frame_count <= 16
			&& cur > ps3gcm_fifo_first_cur)
			ps3gcm_fifo_bytes_per_flip = (cur - ps3gcm_fifo_first_cur) / frame_count;

		if (ps3gcm_fifo_prev_cur != 0 && cur < ps3gcm_fifo_prev_cur)
		{
			ps3gcm_fifo_wraps++;
			ps3gcm_fifo_line("WRAPPED", cur);
			ps3gcm_fifo_prev_cur = cur;
			return;
		}
		ps3gcm_fifo_prev_cur = cur;
	}

	// 16 flips' worth of headroom, clamped: enough warning to see the wrap
	// coming, short enough that the 903us-per-write cost stays negligible.
	danger = ps3gcm_fifo_bytes_per_flip * 16;
	if (danger < 256) danger = 256;
	if (danger > 4096) danger = 4096;

	if (end > cur && (end - cur) <= danger)
	{
		ps3gcm_fifo_line(stage ? "danger/after" : "danger/before", cur);
		return;
	}

	if (stage != 0)
		return;

	if (frame_count < 4)
		ps3gcm_fifo_line("sample", cur);
	else if ((frame_count & 255) == 0)
		ps3gcm_fifo_line("periodic", cur);
}

// ============================================================
// PS3GCM_VideoInit
// ============================================================

void PS3GCM_VideoInit(void)
{
	s32 ret;
	int i;
	u32 fb_size;
	void *host_addr = memalign(1024 * 1024, HOST_SIZE);

	if (!host_addr)
	{
		fprintf(stderr, "[SRB2Kart PS3] Failed to allocate host memory for gcmInitBody\n");
		return;
	}

	ret = gcmInitBody(&context, CMD_SIZE, HOST_SIZE, host_addr);
	if (ret != 0)
	{
		fprintf(stderr, "[SRB2Kart PS3] gcmInitBody failed: 0x%08X\n", (unsigned)ret);
		return;
	}

	// 2026-08-26: replace the command-FIFO wrap callback that gcmInitBody
	// leaves installed.
	//
	// PS3DK does this itself for anyone who goes through cellGcmInit()
	// (<cell/gcm.h>, "We override the firmware default installed by rsxInit /
	// gcmInitBodyEx because it doesn't advance PUT in the wrap sequence").
	// We call gcmInitBody directly, so we never got the replacement, and the
	// FIFO wrap is where this port has been dying:
	//
	//   116 bytes per flip, ~28KB per FIFO segment, so a wrap every ~250
	//   flips -- roughly every four seconds at 60Hz. Instrumenting the wrap
	//   (PS3TRACE_FIFO) caught two runs of the same build failing at one:
	//   one froze right after the switch at flip 768 while the RSX carried on
	//   presenting at 60fps, the other host-segfaulted RPCS3 at flip 1296 with
	//   40 bytes left in the segment and no line written on the far side.
	//
	// It also explains the thing nobody could explain: why only serialisation
	// placed in D_Display's drawing path kept the game alive. Slowing the CPU
	// lets the RSX drain the FIFO, so the wrap lands on an already-consumed
	// buffer instead of racing GET. That makes the sync barriers in D_Display
	// a symptom-level fix for this, and they should be re-tested once this is
	// proven (see the ps3_debugtrace block in d_main.c).
	//
	// ps3tc_fifo_wrap_callback writes the tail JUMP, publishes PUT at begin,
	// and then drains until GET has actually followed the jump before letting
	// us write low FIFO memory again -- which is precisely the race above.
	ps3tc_fifo_wrap_install(context);

	vram_init();

	// ------------------------------------------------------------------
	// Video mode. 2026-08-26: instrumented after the first real-hardware
	// attempt showed a red flash and a drop back to the XMB.
	//
	// This used to force VIDEO_RESOLUTION_720 and ignore every return value.
	// RPCS3 always accepts 720p, so the assumption was never tested; a real
	// console attached to a display that cannot do 720p would leave the RSX
	// scanning out whatever happened to be in memory, which is what a red
	// flash looks like. Ask the console what it can do, say so in the log,
	// and record what we actually got rather than what we asked for.
	{
		videoState vstate;
		videoResolution vres;
		videoConfiguration vconfig;
		s32 rc_state, rc_avail, rc_conf;
		FILE *vf;

		memset(&vstate, 0, sizeof vstate);
		memset(&vres, 0, sizeof vres);

		rc_state = videoGetState(0, 0, &vstate);
		videoGetResolution(vstate.displayMode.resolution, &vres);
		rc_avail = videoGetResolutionAvailability(0, VIDEO_RESOLUTION_720,
			vstate.displayMode.aspect, 0);

		vf = fopen(PS3_DebugPath("psdebugV.txt"), "a");
		if (vf)
		{
			fprintf(vf, "videoGetState rc=%d state=%u colorSpace=%u\n",
				(int)rc_state, (unsigned)vstate.state, (unsigned)vstate.colorSpace);
			fprintf(vf, "  mode actuel: resolution=%u (%ux%u) scanMode=%u aspect=%u refresh=0x%04x\n",
				(unsigned)vstate.displayMode.resolution,
				(unsigned)vres.width, (unsigned)vres.height,
				(unsigned)vstate.displayMode.scanMode,
				(unsigned)vstate.displayMode.aspect,
				(unsigned)vstate.displayMode.refreshRates);
			// Raw value on purpose. Under RPCS3 this returns 0 while 720p
			// then configures perfectly, so the emulator is very likely
			// stubbing it -- do not read 0 as "unavailable" without a real
			// console to compare against. What actually decides the outcome
			// is the mode reported after videoConfigure, further down.
			fprintf(vf, "  videoGetResolutionAvailability(720p) -> %d (brut ; RPCS3 renvoie 0 alors que le 720p marche)\n",
				(int)rc_avail);
			fflush(vf);
		}

		memset(&vconfig, 0, sizeof(vconfig));
		vconfig.resolution = VIDEO_RESOLUTION_720;
		vconfig.format     = VIDEO_BUFFER_FORMAT_XRGB;
		vconfig.pitch      = PS3_SCREEN_W * 4;
		vconfig.aspect     = vstate.displayMode.aspect;
		rc_conf = videoConfigure(0, &vconfig, NULL, 0);

		memset(&vstate, 0, sizeof vstate);
		memset(&vres, 0, sizeof vres);
		videoGetState(0, 0, &vstate);
		videoGetResolution(vstate.displayMode.resolution, &vres);

		if (vf)
		{
			fprintf(vf, "videoConfigure(720p, XRGB, pitch=%d) -> %d\n",
				(int)(PS3_SCREEN_W * 4), (int)rc_conf);
			fprintf(vf, "  mode obtenu: resolution=%u (%ux%u) aspect=%u\n",
				(unsigned)vstate.displayMode.resolution,
				(unsigned)vres.width, (unsigned)vres.height,
				(unsigned)vstate.displayMode.aspect);
			if (vres.width != PS3_SCREEN_W || vres.height != PS3_SCREEN_H)
				fprintf(vf, "  *** ATTENTION: %ux%u au lieu de %dx%d -- le scaler et les"
					" framebuffers sont dimensionnes pour %dx%d ***\n",
					(unsigned)vres.width, (unsigned)vres.height,
					PS3_SCREEN_W, PS3_SCREEN_H, PS3_SCREEN_W, PS3_SCREEN_H);
			fflush(vf);
			fclose(vf);
		}

		fprintf(stderr, "[SRB2Kart PS3] video: demande 720p rc=%d, obtenu %ux%u\n",
			(int)rc_conf, (unsigned)vres.width, (unsigned)vres.height);
	}

	color_pitch = 4 * ((PS3_SCREEN_W + 15) / 16) * 16;

	fb_size = color_pitch * PS3_SCREEN_H;
	for (i = 0; i < PS3_FB_COUNT; i++)
	{
		fb_addr[i] = vram_alloc(64, fb_size);
		gcmAddressToOffset(fb_addr[i], &fb_offset[i]);
		gcmSetDisplayBuffer(i, fb_offset[i], color_pitch, PS3_SCREEN_W, PS3_SCREEN_H);
		memset(fb_addr[i], 0, fb_size);
	}

	// No flip-wait: gcmSetWaitFlip()/polling gcmGetFlipStatus() is what hung
	// (both in our own SDL2_PSL1GHT investigation and independently in
	// TanakaDOOM-cGcm). HSYNC mode removes the internal vsync wait as well.
	gcmSetFlipMode(GCM_FLIP_HSYNC);

	gcmResetFlipStatus();
	gcmSetFlip(context, 1);
	flush_buffer();

	for (i = 0; i < 256; i++)
		palette_argb[i] = 0xFF000000 | (i << 16) | (i << 8) | i;

	letterbox_cleared = 0;

	fprintf(stderr, "[SRB2Kart PS3] cellGcm video initialized: %dx%d output, %dx%d source (HSYNC, no flip-wait)\n",
		PS3_SCREEN_W, PS3_SCREEN_H, SRC_W, SRC_H);
}

void PS3GCM_VideoShutdown(void)
{
	fprintf(stderr, "[SRB2Kart PS3] cellGcm video shutdown\n");
}

// ============================================================
// PS3GCM_SetPalette
// ============================================================

void PS3GCM_SetPalette(const RGBA_t *palette)
{
	int i;
	if (!palette)
		return;

	for (i = 0; i < 256; i++)
	{
		palette_argb[i] = 0xFF000000
			| ((u32)palette[i].s.red   << 16)
			| ((u32)palette[i].s.green << 8)
			|  (u32)palette[i].s.blue;
	}
}

// ============================================================
// clear_letterbox — clear top/bottom bars once, on both buffers
// ============================================================

static void clear_letterbox(void)
{
	u32 pitch_pixels = color_pitch / 4;
	int buf;

	for (buf = 0; buf < PS3_FB_COUNT; buf++)
	{
		u32 *dst = (u32 *)fb_addr[buf];
		u32 y, x;

		for (y = 0; y < OFFSET_Y; y++)
		{
			u32 *row = dst + y * pitch_pixels;
			for (x = 0; x < PS3_SCREEN_W; x++)
				row[x] = 0xFF000000;
		}

		for (y = OFFSET_Y + DISPLAY_H; y < PS3_SCREEN_H; y++)
		{
			u32 *row = dst + y * pitch_pixels;
			for (x = 0; x < PS3_SCREEN_W; x++)
				row[x] = 0xFF000000;
		}
	}
}

// ============================================================
// PS3GCM_FinishUpdate — palette convert + nearest-neighbor scale
// ============================================================

void PS3GCM_FinishUpdate(const UINT8 *src)
{
	u32 *dst;
	u32 pitch_pixels;
	u32 sy;
	int timing = (ps3gcm_timing_calls < PS3GCM_TIMING_FRAMES);
	u64 t0 = 0, t1;

	ps3gcm_timing_calls++;

	if (!src || !fb_addr[fb_index])
		return;

	if (!letterbox_cleared)
	{
		clear_letterbox();
		letterbox_cleared = 1;
	}

	dst = (u32 *)fb_addr[fb_index];
	pitch_pixels = color_pitch / 4;

	if (timing)
		t0 = ps3gcm_now_us();

	for (sy = 0; sy < SRC_H; sy++)
	{
		const UINT8 *src_row = src + sy * SRC_W;
		u32 scanline_argb[SRC_W];
		u32 sx, ry;

		for (sx = 0; sx < SRC_W; sx++)
			scanline_argb[sx] = palette_argb[src_row[sx]];

		for (ry = 0; ry < SCALE_Y; ry++)
		{
			u32 dy = OFFSET_Y + sy * SCALE_Y + ry;
			u32 *dst_row = dst + dy * pitch_pixels;

			for (sx = 0; sx < SRC_W; sx++)
			{
				u32 color = scanline_argb[sx];
				u32 dx = sx * SCALE_X;
				u32 s;
				for (s = 0; s < SCALE_X; s++)
					dst_row[dx + s] = color;
			}
		}
	}

	if (timing)
	{
		t1 = ps3gcm_now_us();
		ps3gcm_log_us("PS3GCM_FinishUpdate (palette convert + scale)", t0, t1);
	}
}

// ============================================================
// PS3GCM_Flip — submit flip, no wait
// ============================================================
//
// Deliberately zero instrumentation here -- see the note above
// PS3GCM_TIMING_FRAMES. This must stay exactly as validated.

void PS3GCM_Flip(void)
{
	// 2026-08-26: the two watch calls are the one exception to the "zero
	// instrumentation here" rule above. They are a handful of loads and
	// compares in the common case (no syscall, no I/O), and they only touch
	// the file system in the ~30 flips per 64KB lap where the FIFO is about
	// to wrap. See the ps3gcm_fifo_watch block for why that window matters.
	ps3gcm_fifo_watch(0);

	gcmSetFlip(context, fb_index);
	flush_buffer();

	ps3gcm_fifo_watch(1);

	fb_index ^= 1;
	frame_count++;
}
