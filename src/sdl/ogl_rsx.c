// SONIC ROBO BLAST 2 KART -- portage PS3
//-----------------------------------------------------------------------------
/// \file  ogl_rsx.c
/// \brief Contexte RSX, framebuffers et presentation, pour la couche ps3gl.
///
/// 2026-09-06 -- remplace sdl/ogl_psgl.c, qui faisait le meme travail au-dessus
/// de PSGL.
///
/// Meme contrat que lui, celui que sdl/ogl_sdl.h impose a tout backend GL de
/// SRB2 : LoadGL(), OglSdlSurface(), OglSdlFinishUpdate(), GetGLFunc(). Le
/// moteur ne voit pas la difference -- r_opengl.c continue d'appeler gl*, et
/// ps3/rsxgl/rsxgl_glapi.c fait la jonction vers ps3gl_*.
///
/// Le squelette RSX vient de IoQuake3-PS3 (code/sys/ps3_glimp.c), avec trois
/// differences deliberees, chacune payee par une soiree de debogage :
///
///  1. ON RELIT LE MODE VIDEO REELLEMENT OBTENU. Eux demandent le 720p et se
///     servent ensuite des dimensions DEMANDEES. Le 05/09, la PS3 d'Alex a
///     accepte videoConfigure(720p) en renvoyant 0 tout en restant en
///     1920x1080 : on a alors configure la sortie a une definition et
///     dimensionne pas et framebuffers pour une autre. Ecran noir garanti.
///     Ici on interroge videoGetState APRES coup et on batit sur la reponse.
///
///  2. Toutes les attentes sont BORNEES. L'attente d'etiquette non bornee de
///     PSGL a gele le demarrage pendant deux jours.
///
///  3. On journalise sur DISQUE (psdebugC.txt). Sur vraie console il n'existe
///     aucun journal d'emulateur : c'est la seule visibilite possible, et les
///     marqueurs C0..C10 restent les memes que du temps de PSGL pour que les
///     traces d'avant et d'apres se comparent.
//-----------------------------------------------------------------------------

#include "../doomdef.h"

#ifdef HWRENDER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#include "SDL.h"

#include "../d_main.h"
#include "../hardware/r_opengl/r_opengl.h"
#include "../hardware/hw_main.h"
#include "ogl_sdl.h"
#include "../i_system.h"
#include "../m_argv.h"
#include "../console.h"

#include "../../ps3/rsxgl/ps3gl.h"

// ==========================================================================
//                                                                    ETAT
// ==========================================================================

// Trois tampons : le CPU en a toujours un de libre pendant que le RSX affiche
// et qu'un troisieme attend son tour. C'est ce qui permet aux deux de
// travailler en parallele au lieu d'additionner leurs temps -- la lecon n°2 du
// portage Vita, ou le double tampon faisait bloquer le CPU a chaque frame.
#define RSX_FB_COUNT      3
#define RSX_FB_ALIGN      64
#define RSX_CB_SIZE       (1 * 1024 * 1024)   // tampon de commandes
#define RSX_HOST_SIZE     (32 * 1024 * 1024)  // tampon d'E/S

// Definie par le backend, pas par r_opengl.c : c'etait deja le cas dans
// ogl_psgl.c, que ce fichier remplace.
INT32 oglflags = 0;

static gcmContextData *rsx_ctx = NULL;

static u32   rsx_width  = 0;
static u32   rsx_height = 0;
static u32   rsx_pitch  = 0;

static u32   rsx_color_offset[RSX_FB_COUNT];
static u32  *rsx_color_buffer[RSX_FB_COUNT];
static u32   rsx_depth_offset;
static u32  *rsx_depth_buffer = NULL;

static int   rsx_cur_fb = 0;
static int   rsx_cur_rt = -1;

static volatile u32 rsx_flip_queued    = 0;
static volatile u32 rsx_flip_completed = 0;

// Dimensions du tampon de rendu, lues apres coup. r_opengl.c en a besoin pour
// le ciseau et la passe d'ecran final -- surtout PAS realwidth/realheight, qui
// decrivent la resolution logique du jeu et non celle de la sortie.
u32 ps3gl_render_w = 0;
u32 ps3gl_render_h = 0;

// ==========================================================================
//                                                             JOURNALISATION
// ==========================================================================

static void ps3gl_log(const char *where)
{
	FILE *f = fopen(PS3_DebugPath("psdebugC.txt"), "a");
	if (f) { fputs(where, f); fputc(10, f); fflush(f); fclose(f); }
}

static void ps3gl_log2(const char *what, unsigned a, unsigned b)
{
	FILE *f = fopen(PS3_DebugPath("psdebugC.txt"), "a");
	if (f) { fprintf(f, "%s %u %u\n", what, a, b); fflush(f); fclose(f); }
}

// La couche reprise d'IoQuake3 attend ce symbole de son hote.
void ps3_log(const char *msg)
{
	FILE *f = fopen(PS3_DebugPath("psdebugR.txt"), "a");
	if (f) { fputs(msg ? msg : "(null)", f); fputc(10, f); fflush(f); fclose(f); }
}

// ==========================================================================
//                                                    RESOLUTION DES POINTEURS
// ==========================================================================

// Sous STATIC_OPENGL les entrees de base se resolvent a l'edition de liens
// (rsxgl_glapi.c les exporte). Seules les neuf de SetupGLFunc4 passent ici, et
// r_opengl.c sait traiter un NULL -- c'est ainsi qu'il decouvre ce que la pile
// ne sait pas faire.
void *GetGLFunc(const char *proc)
{
	(void)proc;
	return NULL;
}

// ==========================================================================
//                                                       FRAMEBUFFERS ET CIBLE
// ==========================================================================

static boolean RSX_AllocFramebuffers(void)
{
	u32 color_size, depth_size;
	int i;

	rsx_pitch  = rsx_width * 4;               // ARGB8888
	color_size = rsx_pitch * rsx_height;
	depth_size = rsx_width * rsx_height * 4;  // Z24S8

	for (i = 0; i < RSX_FB_COUNT; i++)
	{
		rsx_color_buffer[i] = (u32 *)rsxMemalign(RSX_FB_ALIGN, color_size);
		if (!rsx_color_buffer[i])
		{
			ps3gl_log2("RSX ECHEC allocation tampon couleur", (unsigned)i, color_size);
			return false;
		}
		rsxAddressToOffset(rsx_color_buffer[i], &rsx_color_offset[i]);
		gcmSetDisplayBuffer((u8)i, rsx_color_offset[i], rsx_pitch, rsx_width, rsx_height);
	}

	rsx_depth_buffer = (u32 *)rsxMemalign(RSX_FB_ALIGN, depth_size);
	if (!rsx_depth_buffer)
	{
		ps3gl_log2("RSX ECHEC allocation tampon profondeur", depth_size, 0);
		return false;
	}
	rsxAddressToOffset(rsx_depth_buffer, &rsx_depth_offset);
	return true;
}

static void RSX_SetRenderTarget(int index)
{
	gcmSurface sf;
	int i;

	if (index == rsx_cur_rt)
		return;
	rsx_cur_rt = index;

	memset(&sf, 0, sizeof sf);

	sf.colorFormat      = GCM_SURFACE_A8R8G8B8;
	sf.colorTarget      = GCM_SURFACE_TARGET_0;
	sf.colorLocation[0] = GCM_LOCATION_RSX;
	sf.colorOffset[0]   = rsx_color_offset[index];
	sf.colorPitch[0]    = rsx_pitch;

	// Les cibles 1 a 3 ne servent pas, mais le RSX veut des valeurs valides :
	// un pitch nul le fait deraper.
	for (i = 1; i < 4; i++)
	{
		sf.colorLocation[i] = GCM_LOCATION_RSX;
		sf.colorOffset[i]   = rsx_color_offset[index];
		sf.colorPitch[i]    = 64;
	}

	sf.depthFormat   = GCM_SURFACE_ZETA_Z24S8;
	sf.depthLocation = GCM_LOCATION_RSX;
	sf.depthOffset   = rsx_depth_offset;
	sf.depthPitch    = rsx_width * 4;

	sf.type      = GCM_SURFACE_TYPE_LINEAR;
	sf.antiAlias = GCM_SURFACE_CENTER_1;

	sf.width  = rsx_width;
	sf.height = rsx_height;
	sf.x      = 0;
	sf.y      = 0;

	rsxSetSurface(rsx_ctx, &sf);
}

static void RSX_FlipHandler(const u32 head)
{
	(void)head;
	rsx_flip_completed++;
}

// Ne bloque que si les deux tampons non affiches sont en vol. Bornee : au bout
// de ~2 s on force la synchronisation plutot que de figer le jeu. C'est la
// lecon de psgl_wait_rsx_idle, dont l'attente non bornee a coute deux jours.
static void RSX_WaitFlips(void)
{
	int waited = 0;

	while ((int)(rsx_flip_queued - rsx_flip_completed) > RSX_FB_COUNT - 2)
	{
		usleep(100);
		if (++waited > 20000)
		{
			ps3gl_log("RSX attente de flip expiree, synchronisation forcee");
			rsx_flip_completed = rsx_flip_queued;
			break;
		}
	}
}

// ==========================================================================
//                                                            INITIALISATION
// ==========================================================================

boolean LoadGL(void)
{
	void *host_addr;
	videoState state;
	videoResolution res;
	videoConfiguration vconfig;
	s32 ret;

	ps3gl_log("C0 LoadGL entry (RSX)");

	if (rsx_ctx)
		return true;

	host_addr = memalign(1024 * 1024, RSX_HOST_SIZE);
	if (!host_addr)
	{
		ps3gl_log("C0a ECHEC memalign du tampon d'E/S");
		return false;
	}

	ret = rsxInit(&rsx_ctx, RSX_CB_SIZE, RSX_HOST_SIZE, host_addr);
	ps3gl_log2("C1 rsxInit ret/ctx", (unsigned)ret, (unsigned)(uintptr_t)rsx_ctx);
	if (ret != 0 || !rsx_ctx)
	{
		// Un contexte gcm peut deja exister si quelque chose l'a initialise
		// avant nous. On tente de le recuperer plutot que d'abandonner.
		rsxSetDefaultCommandBuffer(&rsx_ctx);
		ps3gl_log2("C1a repli contexte par defaut", (unsigned)(uintptr_t)rsx_ctx, 0);
		if (!rsx_ctx)
		{
			ps3gl_log("C1b ECHEC : aucun contexte RSX");
			return false;
		}
	}

	// --- Definition ---------------------------------------------------------
	//
	// On DEMANDE le 720p, mais on ne le suppose jamais obtenu. Le 05/09, cette
	// console a renvoye 0 a videoConfigure(720p) tout en restant en 1920x1080.
	memset(&state, 0, sizeof state);
	if (videoGetState(VIDEO_PRIMARY, 0, &state) != 0)
	{
		ps3gl_log("C1c ECHEC videoGetState");
		return false;
	}
	ps3gl_log2("C1d mode courant resolution/aspect",
		(unsigned)state.displayMode.resolution, (unsigned)state.displayMode.aspect);

	memset(&res, 0, sizeof res);
	if (videoGetResolution(state.displayMode.resolution, &res) != 0)
	{
		ps3gl_log("C1e ECHEC videoGetResolution");
		return false;
	}

	memset(&vconfig, 0, sizeof vconfig);
	vconfig.resolution = state.displayMode.resolution;
	vconfig.format     = VIDEO_BUFFER_FORMAT_XRGB;
	vconfig.pitch      = res.width * 4;
	vconfig.aspect     = state.displayMode.aspect;
	if (videoConfigure(VIDEO_PRIMARY, &vconfig, NULL, 0) != 0)
	{
		ps3gl_log("C1f ECHEC videoConfigure");
		return false;
	}

	// Et on RELIT ce qu'on a vraiment obtenu.
	memset(&state, 0, sizeof state);
	videoGetState(VIDEO_PRIMARY, 0, &state);
	videoGetResolution(state.displayMode.resolution, &res);

	rsx_width  = res.width;
	rsx_height = res.height;
	ps3gl_render_w = rsx_width;
	ps3gl_render_h = rsx_height;
	ps3gl_log2("C1g definition obtenue", rsx_width, rsx_height);

	if (!RSX_AllocFramebuffers())
		return false;
	ps3gl_log("C1h framebuffers alloues");

	rsx_cur_fb         = 0;
	rsx_flip_queued    = 0;
	rsx_flip_completed = 0;
	gcmSetFlipHandler(RSX_FlipHandler);
	RSX_SetRenderTarget(rsx_cur_fb);
	ps3gl_log("C2 cible de rendu posee");

	ps3gl_init(rsx_ctx, rsx_width, rsx_height);
	ps3gl_log("C3 ps3gl_init termine");

	return SetupGLfunc();
}

boolean OglSdlSurface(INT32 w, INT32 h)
{
	static boolean first_init = false;

	oglflags = 0;

	if (!rsx_ctx && !LoadGL())
		return false;

	if (!first_init)
	{
		gl_version    = pglGetString(GL_VERSION);
		gl_renderer   = pglGetString(GL_RENDERER);
		gl_extensions = pglGetString(GL_EXTENSIONS);

		GL_DBG_Printf("OpenGL %s\n", gl_version);
		GL_DBG_Printf("GPU: %s\n", gl_renderer);
		GL_DBG_Printf("Extensions: %s\n", gl_extensions);
		first_init = true;
	}
	ps3gl_log("C5 after pglGetString trio");

	// Pas d'anisotropie : l'extension n'est pas annoncee, et interroger un
	// glGetIntegerv qui ignore la constante laisserait la variable telle
	// quelle. C'est exactement le defaut que GL_VIEWPORT nous a appris.
	maximumAnisotropy = 1;

	SetupGLFunc4();
	ps3gl_log("C6 after SetupGLFunc4");

	granisotropicmode_cons_t[1].value = maximumAnisotropy;

	SetModelView(w, h);
	ps3gl_log("C7 after SetModelView");
	SetStates();
	ps3gl_log("C8 after SetStates");
	pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	ps3gl_log("C9 after pglClear");

	HWR_Startup();
	ps3gl_log("C10 after HWR_Startup");
	textureformatGL = GL_RGBA;

	return true;
}

// ==========================================================================
//                                                              PRESENTATION
// ==========================================================================

void OglSdlFinishUpdate(boolean waitvbl)
{
	(void)waitvbl; // le flip se cale sur le vblank

	// Battement de la couche GL, sur disque. Il repond a une seule question,
	// celle qui a coute le plus de temps : quand ca se fige, est-ce la boucle
	// de jeu ou la presentation ? psdebugS.txt bat cote jeu, celui-ci compte
	// les echanges reellement termines.
	{
		static unsigned long swaps = 0;

		if ((swaps % 60UL) == 0UL)
		{
			FILE *f = fopen(PS3_DebugPath("psdebugGL.txt"), "a");
			if (f) { fprintf(f, "swap %lu\n", swaps); fflush(f); fclose(f); }
		}
		swaps++;
	}

	if (!rsx_ctx)
		return;

	// La passe d'ecran final reste sautee tant que glCopyTexImage2D n'est pas
	// implemente : elle recouvrirait la scene avec une texture vide. Meme
	// symptome que le blocage n°4 du portage Vita -- le jeu tourne, le son
	// joue, l'ecran est noir. Voir ps3/rsxgl/rsxgl_glapi.c.
	ps3gl_end_frame();

	gcmSetWaitFlip(rsx_ctx);
	gcmSetFlip(rsx_ctx, (u8)rsx_cur_fb);
	rsxFlushBuffer(rsx_ctx);

	rsx_flip_queued++;
	rsx_cur_fb = (rsx_cur_fb + 1) % RSX_FB_COUNT;

	// Le prochain dessin va dans le tampon suivant : on attend qu'il soit
	// libre, puis on rebascule la cible.
	RSX_WaitFlips();
	RSX_SetRenderTarget(rsx_cur_fb);
	ps3gl_begin_frame();
}

void OglPS3Shutdown(void)
{
	int i;

	if (!rsx_ctx)
		return;

	rsx_cur_rt = -1;
	ps3gl_shutdown();
	rsxFinish(rsx_ctx, 1);

	for (i = 0; i < RSX_FB_COUNT; i++)
	{
		if (rsx_color_buffer[i])
		{
			rsxFree(rsx_color_buffer[i]);
			rsx_color_buffer[i] = NULL;
		}
	}
	if (rsx_depth_buffer)
	{
		rsxFree(rsx_depth_buffer);
		rsx_depth_buffer = NULL;
	}
	rsx_ctx = NULL;
}

// ==========================================================================
//                                                                  PALETTE
// ==========================================================================

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette, RGBA_t *pgamma)
{
	// Le chemin materiel travaille en RGBA : la palette ne sert qu'a convertir
	// les patches 8 bits, et hw_cache.c s'en charge deja.
	(void)palette;
	(void)pgamma;
}

#endif // HWRENDER
