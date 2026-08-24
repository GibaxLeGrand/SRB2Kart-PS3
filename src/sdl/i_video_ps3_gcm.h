// i_video_ps3_gcm.h — SRB2Kart PS3 video backend, raw cellGcm (no SDL)
//
// Replaces SDL2_PSL1GHT for video output on PS3. SDL2_PSL1GHT's flip
// synchronization (gcmSetFlip/gcmSetWaitFlip) hangs deterministically after
// ~555 cumulative flips under RPCS3 (see project memory). TanakaDOOM-cGcm
// (github.com/kan8223-dotcom/TanakaDOOM-cGcm) independently hit and worked
// around the same gcmSetWaitFlip class of bug using raw cellGcm with no
// flip-wait at all; this backend follows that proven pattern.
//
// Doom-family engines (SRB2Kart included) render into an 8-bit paletted
// software framebuffer at BASEVIDWIDTH x BASEVIDHEIGHT (320x200, forced
// resolution on PS3 — see VID_SetMode() in i_video.c). This backend converts
// that buffer to ARGB32, scales it with CPU nearest-neighbor into a VRAM
// display buffer, and flips it — no SDL, no GPU draw calls.

#ifndef I_VIDEO_PS3_GCM_H
#define I_VIDEO_PS3_GCM_H

#include "../doomtype.h"

// Initialize cellGcm, allocate VRAM display buffers.
void PS3GCM_VideoInit(void);

void PS3GCM_VideoShutdown(void);

// palette: 256-entry RGBA_t array (as passed to I_SetPalette).
void PS3GCM_SetPalette(const RGBA_t *palette);

// src: BASEVIDWIDTH x BASEVIDHEIGHT 8-bit paletted buffer (screens[0]).
// Converts + scales into the current VRAM back buffer.
void PS3GCM_FinishUpdate(const UINT8 *src);

// Submits the flip command for the buffer written by PS3GCM_FinishUpdate()
// and swaps to the other buffer. Does not wait for the flip to complete.
void PS3GCM_Flip(void);

#endif // I_VIDEO_PS3_GCM_H
