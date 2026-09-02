// SONIC ROBO BLAST 2 KART -- portage PS3
//-----------------------------------------------------------------------------
// Distribue sous licence GNU General Public License, version 2.
// Voir le fichier 'LICENSE'.
//-----------------------------------------------------------------------------
/// \file
/// \brief Partie PS3/PSGL de l'API OpenGL -- l'equivalent de ogl_sdl.c
///
/// ogl_sdl.c s'appuie sur SDL pour trois choses que SDL2_PSL1GHT ne sait pas
/// faire ici : creer un contexte GL, resoudre les pointeurs de fonction, et
/// presenter la frame. PSGL fait les trois lui-meme, alors ce fichier le
/// remplace au lieu de le contourner.
///
/// Ce que ce fichier N'EST PAS : une couche de traduction. PSGL est une vraie
/// implementation OpenGL ES 1.1 (verifie a l'execution le 02/09/2026, voir
/// ps3/psgl_probe/) et r_opengl.c compile contre elle sans un seul symbole
/// manquant. IoQuake3-PS3 a du ecrire 2903 lignes de GL->cellGcm parce que
/// PSL1GHT n'a pas PSGL ; nous n'avons besoin que de la glue ci-dessous.

#include "../doomdef.h"

#if defined(_PS3) && defined(HWRENDER)

#include <PSGL/psgl.h>

// SDL reste dans le decor meme si PSGL remplace sa partie GL : la couche video
// PS3 est toujours celle de sdl/, et ogl_sdl.h -- dont on reprend les
// prototypes pour rester interchangeable avec ogl_sdl.c -- declare
// realwidth/realheight en Uint16 et deux types SDL.
#include "SDL.h"

#include "../d_main.h"
#include "../hardware/r_opengl/r_opengl.h"
#include "../hardware/hw_main.h"
#include "ogl_sdl.h"
#include "../i_system.h"
#include "../m_argv.h"
#include "../console.h"

// ==========================================================================
//                                                                    ETAT
// ==========================================================================

INT32 oglflags = 0;
void *GLUhandle = NULL;

static PSGLdevice  *ps3gl_device;
static PSGLcontext *ps3gl_context;
static GLuint       ps3gl_render_w, ps3gl_render_h;

// Resolution de RENDU. A distinguer de la sortie : PSGL les separe, et c'est
// tout l'interet.
//
//   PSGL_DEVICE_PARAMETERS_WIDTH_HEIGHT             -> la SORTIE
//   PSGL_DEVICE_PARAMETERS_RESC_RENDER_WIDTH_HEIGHT -> ce qu'on REND
//
// Mesure du 02/09 : ne poser que le premier donne un device a 1280x720 et un
// renderbuffer reste a 1920x1080 (psgl_context.c:2509-2521). Il faut les deux.
//
// C'est aussi, tel quel, le mode performance du portage PS Vita : rendre
// petit, laisser le materiel monter a l'echelle vers la sortie. Chez eux
// 640x368 rendu pour 960x544 affiche, et la presentation est passee de 14,5 ms
// a 7,4 ms -- exactement proportionnel aux pixels. Ici le scaler est RESC,
// cote RSX, donc sans un cycle de PPE.
#define PS3GL_OUT_W 1280u
#define PS3GL_OUT_H 720u

// ==========================================================================
//                                              RESOLUTION DES POINTEURS
// ==========================================================================

// L'equivalent du qgl_ps3.c d'IoQuake3-PS3 -- en neuf entrees au lieu de leurs
// soixante, parce qu'ils reliaient un renderer ecrit a la main la ou nous
// pointons vers une bibliotheque deja liee.
//
// SetupGLfunc() ne fait rien sous STATIC_OPENGL (tout est resolu a l'edition
// de liens). Seul SetupGLFunc4() reste, et il demande exactement ces neuf
// noms plus gluBuild2DMipmaps. Table statique : pas de chargeur dynamique sur
// cette console, et il n'en faut pas.
//
// ⚠ Deux de ces entrees pointent vers des fonctions que PSGL exporte mais dont
// le CORPS EST VIDE (verifie dans core_gl.c, pas suppose) :
//   - glMultiTexCoord4f, que nos deux shims glMultiTexCoord2f* appellent
//   - glTexEnvi (resolu au lien, pas ici, mais meme probleme)
// On les branche quand meme : renvoyer NULL ferait echouer SetupGLFunc4 alors
// que le reste marche. Ce sont les trous connus, listes dans
// AUDIT_VITA_20260902.md ; ils se voient a l'image, pas au lien.

typedef struct { const char *name; void *addr; } ps3gl_entry_t;

static const ps3gl_entry_t ps3gl_entries[] =
{
	{ "glActiveTexture",       (void *)glActiveTexture       },
	{ "glClientActiveTexture", (void *)glClientActiveTexture },
	{ "glMultiTexCoord2f",     (void *)glMultiTexCoord2f     }, // -> shim glcompat
	{ "glMultiTexCoord2fv",    (void *)glMultiTexCoord2fv    }, // -> shim glcompat
	{ "glGenBuffers",          (void *)glGenBuffers          },
	{ "glBindBuffer",          (void *)glBindBuffer          },
	{ "glBufferData",          (void *)glBufferData          },
	{ "glDeleteBuffers",       (void *)glDeleteBuffers       },
	{ "glColorPointer",        (void *)glColorPointer        },
	{ "gluBuild2DMipmaps",     (void *)gluBuild2DMipmaps     },
	{ NULL, NULL }
};

void *GetGLFunc(const char *proc)
{
	const ps3gl_entry_t *e;

	if (!proc)
		return NULL;

	for (e = ps3gl_entries; e->name; e++)
	{
		if (strcmp(proc, e->name) == 0)
			return e->addr;
	}

	// Pas une erreur en soi : r_opengl.c demande aussi les entrees shaders
	// quand GL_SHADERS est compile, et il sait traiter un NULL.
	GL_DBG_Printf("GetGLFunc: %s introuvable dans la table PSGL\n", proc);
	return NULL;
}

// ==========================================================================
//                                                            INITIALISATION
// ==========================================================================

boolean LoadGL(void)
{
	PSGLinitOptions options;
	PSGLdeviceParameters params;

	if (ps3gl_device)
		return SetupGLfunc(); // deja initialise

	memset(&options, 0, sizeof options);
	psglInit(&options);

	memset(&params, 0, sizeof params);
	params.enable = PSGL_DEVICE_PARAMETERS_WIDTH_HEIGHT
	              | PSGL_DEVICE_PARAMETERS_RESC_RENDER_WIDTH_HEIGHT;
	params.width        = PS3GL_OUT_W;
	params.height       = PS3GL_OUT_H;
	params.renderWidth  = PS3GL_OUT_W;
	params.renderHeight = PS3GL_OUT_H;

	ps3gl_device = psglCreateDeviceExtended(&params);

	if (!ps3gl_device)
	{
		// Repli : laisser PSGL choisir le mode video de la console plutot que
		// d'abandonner le rendu materiel pour une resolution refusee.
		CONS_Alert(CONS_WARNING,
			"PSGL: %ux%u refuse, on prend le mode automatique.\n",
			PS3GL_OUT_W, PS3GL_OUT_H);
		ps3gl_device = psglCreateDeviceAuto(0, 0, 0);
	}

	if (!ps3gl_device)
	{
		CONS_Alert(CONS_ERROR, "PSGL: aucun peripherique. Retour au rendu logiciel.\n");
		return false;
	}

	ps3gl_context = psglGetCurrentContext();
	if (!ps3gl_context)
		ps3gl_context = psglCreateContext();

	if (!ps3gl_context)
	{
		CONS_Alert(CONS_ERROR, "PSGL: aucun contexte. Retour au rendu logiciel.\n");
		psglDestroyDevice(ps3gl_device);
		ps3gl_device = NULL;
		return false;
	}

	psglMakeCurrent(ps3gl_context, ps3gl_device);
	psglGetRenderBufferDimensions(ps3gl_device, &ps3gl_render_w, &ps3gl_render_h);

	CONS_Printf("PSGL: rendu %ux%u, sortie %ux%u\n",
		(unsigned)ps3gl_render_w, (unsigned)ps3gl_render_h,
		PS3GL_OUT_W, PS3GL_OUT_H);

	return SetupGLfunc();
}

boolean OglSdlSurface(INT32 w, INT32 h)
{
	static boolean first_init = false;

	oglflags = 0;

	if (!ps3gl_device && !LoadGL())
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

	// Pas d'anisotropie sur cette pile : l'extension n'est pas annoncee, et
	// demander GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT a un glGetIntegerv qui ne
	// connait pas la constante laisserait la variable telle quelle -- c'est
	// exactement le defaut que GL_VIEWPORT nous a appris.
	maximumAnisotropy = 1;

	SetupGLFunc4();

	granisotropicmode_cons_t[1].value = maximumAnisotropy;

	SetModelView(w, h);
	SetStates();
	pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	HWR_Startup();
	textureformatGL = GL_RGBA;

	return true;
}

// ==========================================================================
//                                                              PRESENTATION
// ==========================================================================

void OglSdlFinishUpdate(boolean waitvbl)
{
	(void)waitvbl; // psglSwap se cale sur le vblank ; pas d'intervalle a regler

	HWR_MakeScreenFinalTexture();
	HWR_DrawScreenFinalTexture((int)ps3gl_render_w, (int)ps3gl_render_h);

	psglSwap();

	GClipRect(0, 0, realwidth, realheight, NZCLIP_PLANE);

	// Comme dans ogl_sdl.c : on redessine la texture d'ecran final dans
	// l'autre tampon, a sa position d'origine, pour que les effets qui lisent
	// l'ecran precedent puissent le faire apres ce point.
	HWR_DrawScreenFinalTexture(realwidth, realheight);
}

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette, RGBA_t *pgamma)
{
	INT32 i;
	UINT32 redgamma = pgamma->s.red, greengamma = pgamma->s.green,
		bluegamma = pgamma->s.blue;

	for (i = 0; i < 256; i++)
	{
		myPaletteData[i].s.red   = (UINT8)MIN((palette[i].s.red   * redgamma)  /127, 255);
		myPaletteData[i].s.green = (UINT8)MIN((palette[i].s.green * greengamma)/127, 255);
		myPaletteData[i].s.blue  = (UINT8)MIN((palette[i].s.blue  * bluegamma) /127, 255);
		myPaletteData[i].s.alpha = palette[i].s.alpha;
	}
	Flush();
}

void OglPS3Shutdown(void)
{
	if (!ps3gl_device)
		return;

	psglDestroyContext(ps3gl_context);
	psglDestroyDevice(ps3gl_device);
	psglExit();

	ps3gl_context = NULL;
	ps3gl_device = NULL;
}

#endif // _PS3 && HWRENDER
