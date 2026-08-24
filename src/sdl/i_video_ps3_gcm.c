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

#include "i_video_ps3_gcm.h"
#include "../screen.h" // BASEVIDWIDTH, BASEVIDHEIGHT

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

	vram_init();

	{
		videoConfiguration vconfig;
		memset(&vconfig, 0, sizeof(vconfig));
		vconfig.resolution = VIDEO_RESOLUTION_720;
		vconfig.format     = VIDEO_BUFFER_FORMAT_XRGB;
		vconfig.pitch      = PS3_SCREEN_W * 4;
		videoConfigure(0, &vconfig, NULL, 0);
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

	if (!src || !fb_addr[fb_index])
		return;

	if (!letterbox_cleared)
	{
		clear_letterbox();
		letterbox_cleared = 1;
	}

	dst = (u32 *)fb_addr[fb_index];
	pitch_pixels = color_pitch / 4;

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
}

// ============================================================
// PS3GCM_Flip — submit flip, no wait
// ============================================================

void PS3GCM_Flip(void)
{
	gcmSetFlip(context, fb_index);
	flush_buffer();

	fb_index ^= 1;
	frame_count++;
}
