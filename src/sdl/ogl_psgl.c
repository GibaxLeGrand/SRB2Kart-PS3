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
// 2026-09-05 -- PS3GL_OUT_W/H sont RETIRES. Les forcer configurait la sortie a
// la definition de la console tout en dimensionnant le pas et les framebuffers
// pour 1280x720 : ecran noir sur une PS3 en 1080p (voir LoadGL). Le rendu suit
// desormais le mode video en cours. Rendre plus petit que la sortie reste
// souhaitable pour la performance -- c'est la lecon du portage Vita ci-dessus --
// mais cela demande un vrai RESC dans libPSGL, que PS3DK n'implemente pas.

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

// 2026-09-03 -- journal des etapes d'init GL, ecrit sur DISQUE.
//
// Remplace le canari de tas du 02/09 (quinze malloc/free et autant d'ecritures
// par point de passage) : il avait rempli son role en encadrant la corruption
// entre C4 et C5, et coutait bien trop cher pour rester.
//
// Une ligne par etape, avec fflush + fclose immediats : la trace survit donc a
// un gel, ce qui est exactement ce qu'il faut. C'est notre SEULE visibilite sur
// VRAIE CONSOLE, ou aucun journal d'emulateur n'existe -- recuperer
// psdebugC.txt par FTP dans /dev_hdd0/game/<APPID>/USRDIR/.
static void ps3gl_log(const char *where)
{
	FILE *f = fopen(PS3_DebugPath("psdebugC.txt"), "a");
	if (f) { fprintf(f, "%s\n", where); fflush(f); fclose(f); }
}

// 2026-09-05 -- crochet de trace de PSGL (PS3DK, psgl_context.c).
//
// psgl_device_create ne revenait jamais sur vraie console (paquet H : la trace
// s'arretait apres psglInit). PS3DK expose desormais ce pointeur pour qu'on
// voie l'interieur de la bibliotheque ; on y branche ps3gl_log, donc les
// marqueurs D1..D10 de PSGL atterrissent dans psdebugC.txt, entremeles aux
// notres. Declare ici plutot que dans un en-tete PS3DK : c'est un point
// d'observation, pas une API.
extern void (*psgl_trace_hook)(const char *);

boolean LoadGL(void)
{
	ps3gl_log("C0 LoadGL entry");
	psgl_trace_hook = ps3gl_log;
	PSGLinitOptions options;
	PSGLdeviceParameters params;

	if (ps3gl_device)
		return SetupGLfunc(); // deja initialise

	memset(&options, 0, sizeof options);
	// 2026-09-03 -- FIFO laisse a sa taille par defaut (1 Mio).
	//
	// Un test a 8 Mio (x8) a ete fait pour verifier si la mort du FIFO venait
	// de son BOUCLAGE : elle survient de facon reproductible vers 1:20-1:35
	// avec "last cmd" valant un FLOTTANT (-1.0f, +1.0f, -0.9f), donc nos
	// propres coordonnees de sommets lues comme une commande, et l'arithmetique
	// du remplissage tombait pile dans cette fenetre.
	//
	// VERDICT : le temps de survie n'a PAS suivi la taille -- crash a 1:20 avec
	// 8 Mio comme avec 1 Mio. Le bouclage du FIFO est donc HORS DE CAUSE ; ne
	// pas y revenir. La piste restante est un methode emise avec un mauvais
	// compte de mots, dont un parametre flottant finit lu comme en-tete.
	psglInit(&options);
	ps3gl_log("C1 after psglInit");

	// 2026-09-05 -- ON NE FORCE PLUS 1280x720. C'est ce qui donnait l'ecran noir
	// sur vraie console.
	//
	// psgl_device_create() (PS3DK, psgl_context.c:2653) part de la definition
	// reelle lue par videoGetState, puis nos parametres l'ecrasaient :
	//
	//     device->render_width = parameters->renderWidth;        // 1280
	//     device->pitch        = align(render_width * 4, 64);    // 5120
	//     config.resolution    = state.displayMode.resolution;   // reste 1080p
	//     config.pitch         = device->pitch;                  // 5120
	//     videoConfigure(VIDEO_PRIMARY, &config, NULL, 0);
	//
	// Autrement dit on configurait la sortie en 1920x1080 avec le pas d'une
	// ligne de 1280 pixels, et on allouait les framebuffers en 1280x720. Le RSX
	// balayait 1080 lignes de 7680 octets dans un tampon de 720 lignes de 5120.
	// Ecran noir garanti.
	//
	// Sous RPCS3 la console rapporte 720p, donc tout concordait et le bug est
	// reste invisible. La PS3 d'Alex sort en 1920x1080 (psdebugV.txt du 05/09 :
	// "mode obtenu: resolution=1 (1920x1080)"), et plus rien ne concorde.
	//
	// PSGL_DEVICE_PARAMETERS_RESC_RENDER_WIDTH_HEIGHT donne le change mais PS3DK
	// ne cable AUCUN RESC : il se contente de changer le pas. Rendre plus petit
	// que la sortie demanderait donc d'implementer le rescaler dans libPSGL,
	// c'est un autre chantier. En attendant, on prend la definition de la
	// console, ce qui est correct partout.
	memset(&params, 0, sizeof params);
	params.enable = 0; // aucune surcharge : PSGL prend le mode video en cours

	ps3gl_device = psglCreateDeviceExtended(&params);
	ps3gl_log("C1a after psglCreateDeviceExtended");

	if (!ps3gl_device)
	{
		// Repli : psglCreateDeviceAuto renseigne les formats couleur/profondeur
		// et le triple tampon, la ou notre appel etendu les laisse a zero.
		CONS_Alert(CONS_WARNING,
			"PSGL: creation etendue refusee, on prend le mode automatique.\n");
		ps3gl_device = psglCreateDeviceAuto(0, 0, 0);
		ps3gl_log("C1b after psglCreateDeviceAuto");
	}

	if (!ps3gl_device)
	{
		CONS_Alert(CONS_ERROR, "PSGL: aucun peripherique. Retour au rendu logiciel.\n");
		return false;
	}

	ps3gl_log("C1c device obtenu");

	ps3gl_context = psglGetCurrentContext();
	if (!ps3gl_context)
		ps3gl_context = psglCreateContext();
	ps3gl_log("C1d after psglCreateContext");

	if (!ps3gl_context)
	{
		CONS_Alert(CONS_ERROR, "PSGL: aucun contexte. Retour au rendu logiciel.\n");
		psglDestroyDevice(ps3gl_device);
		ps3gl_device = NULL;
		return false;
	}

	ps3gl_log("C2 after psglCreateDeviceExtended/Context");

	psglMakeCurrent(ps3gl_context, ps3gl_device);
	ps3gl_log("C3 after psglMakeCurrent");

	psglGetRenderBufferDimensions(ps3gl_device, &ps3gl_render_w, &ps3gl_render_h);
	{
		// La definition obtenue est l'information qui manquait : elle dit si la
		// console est en 720p ou en 1080p, et donc a quelle taille on dessine.
		GLuint dw = 0, dh = 0;
		char line[96];
		psglGetDeviceDimensions(ps3gl_device, &dw, &dh);
		snprintf(line, sizeof line,
			"C3b peripherique %ux%u, tampon de rendu %ux%u",
			(unsigned)dw, (unsigned)dh,
			(unsigned)ps3gl_render_w, (unsigned)ps3gl_render_h);
		ps3gl_log(line);
	}

	CONS_Printf("PSGL: rendu %ux%u (mode video de la console)\n",
		(unsigned)ps3gl_render_w, (unsigned)ps3gl_render_h);

	{
		boolean r = SetupGLfunc();
		ps3gl_log("C4 after SetupGLfunc");
		return r;
	}
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
	ps3gl_log("C5 after pglGetString trio");

	// Pas d'anisotropie sur cette pile : l'extension n'est pas annoncee, et
	// demander GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT a un glGetIntegerv qui ne
	// connait pas la constante laisserait la variable telle quelle -- c'est
	// exactement le defaut que GL_VIEWPORT nous a appris.
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
	(void)waitvbl; // psglSwap se cale sur le vblank ; pas d'intervalle a regler

	// 2026-09-03 -- battement de la couche GL, sur disque, pour la VRAIE
	// CONSOLE (aucun journal d'emulateur la-bas).
	//
	// Il repond a UNE question, celle qui a coute le plus de temps sous RPCS3 :
	// quand ca se fige, est-ce la boucle de jeu qui s'arrete, ou l'echange
	// d'image ? psdebugS.txt bat une fois par seconde cote jeu ; celui-ci
	// compte les echanges REELLEMENT termines. Si psdebugS avance et pas
	// celui-ci, le blocage est dans psglSwap ; s'ils s'arretent ensemble, il
	// est en amont.
	//
	// Une ligne toutes les 60 images, avec fflush + fclose : la trace survit au
	// gel. Recuperer psdebugGL.txt par FTP dans /dev_hdd0/game/<APPID>/USRDIR/.
	{
		static unsigned long ps3gl_swaps = 0;

		if ((ps3gl_swaps % 60UL) == 0UL)
		{
			FILE *f = fopen(PS3_DebugPath("psdebugGL.txt"), "a");
			if (f)
			{
				fprintf(f, "swap %lu\n", ps3gl_swaps);
				fflush(f);
				fclose(f);
			}
		}
		ps3gl_swaps++;
	}

#if defined(_PS3) && !defined(PS3_USE_FINAL_TEXTURE)
	// 2026-09-02 -- on saute la texture d'ecran finale sur PS3.
	//
	// MakeScreenFinalTexture() capture l'ecran avec
	//   pglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ...)   (r_opengl.c:3549)
	// et DrawScreenFinalTexture() la redessine ensuite par-dessus tout l'ecran,
	// juste avant le swap.
	//
	// Or le glCopyTexImage2D de PSGL ne sait copier QUE le tampon de
	// profondeur : psgl_depth_copy_internal_format() renvoie 0 pour tout ce qui
	// n'est pas GL_DEPTH_COMPONENT*, et la fonction sort sans rien faire
	// (verifie dans psgl_context.c, pas suppose). La texture reste donc vide,
	// et on recouvrait la scene avec du vide a chaque frame -- 60 fps, ecran
	// noir.
	//
	// Consequence assumee : les effets qui relisent l'ecran precedent (wipes)
	// ne fonctionneront pas tant que la copie couleur n'est pas implementee
	// cote PSGL. Rebasculer avec -DPS3_USE_FINAL_TEXTURE.
	psglSwap();
#else
	HWR_MakeScreenFinalTexture();
	HWR_DrawScreenFinalTexture((int)ps3gl_render_w, (int)ps3gl_render_h);

	psglSwap();
#endif

	// 2026-09-02 -- ps3gl_render_w/h, PAS realwidth/realheight.
	//
	// ogl_sdl.c utilise realwidth/realheight parce que sur PC ce SONT les
	// dimensions du contexte GL. Ici non : PSGL rend en 1280x720 (ligne
	// ci-dessus, deja correcte) tandis que realwidth/realheight suivent
	// vid.width/height, soit 320x200. Le ciseau restait donc pose sur un coin
	// de 320x200 et n'etait jamais retabli avant la frame suivante : tout le
	// rendu d'apres se retrouvait decoupe dans ce coin. Symptome : ecran noir
	// avec un liseré sur le bord gauche.
	GClipRect(0, 0, (INT32)ps3gl_render_w, (INT32)ps3gl_render_h, NZCLIP_PLANE);

#if !defined(_PS3) || defined(PS3_USE_FINAL_TEXTURE)
	// Comme dans ogl_sdl.c : on redessine la texture d'ecran final dans
	// l'autre tampon, a sa position d'origine, pour que les effets qui lisent
	// l'ecran precedent puissent le faire apres ce point.
	//
	// 2026-09-02 -- sautee sur PS3 pour la meme raison que ci-dessus : la
	// texture est vide, donc ce dessin ne fait que repeindre du noir.
	HWR_DrawScreenFinalTexture((int)ps3gl_render_w, (int)ps3gl_render_h);
#endif
}

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette, RGBA_t *pgamma)
{
	INT32 i;
	UINT32 redgamma = pgamma->s.red, greengamma = pgamma->s.green,
		bluegamma = pgamma->s.blue;

	PS3_Mark("SM7 OglSdlSetPalette entry");
	for (i = 0; i < 256; i++)
	{
		myPaletteData[i].s.red   = (UINT8)MIN((palette[i].s.red   * redgamma)  /127, 255);
		myPaletteData[i].s.green = (UINT8)MIN((palette[i].s.green * greengamma)/127, 255);
		myPaletteData[i].s.blue  = (UINT8)MIN((palette[i].s.blue  * bluegamma) /127, 255);
		myPaletteData[i].s.alpha = palette[i].s.alpha;
	}
	PS3_Mark("SM8 palette loop done, before Flush");
	Flush();
	PS3_Mark("SM9 Flush done");
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
