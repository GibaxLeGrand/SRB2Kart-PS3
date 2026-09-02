// psgl_probe -- 2026-09-02
//
// Repond aux etapes A2 et A3 de AUDIT_VITA_20260902.md en un seul run :
//
//   A2  PSGL s'initialise-t-il vraiment, et peut-on lui IMPOSER du 1280x720 ?
//   A3  que coute un glDrawArrays texture en pipeline FIXE a cette resolution ?
//
// Construit d'apres hello-psgl-ffp-quad de PS3DK, qui est l'exemple qui compte :
// il dessine en fixed-function pur -- glMatrixMode / glOrthof / glEnable
// (GL_TEXTURE_2D) / glVertexPointer / glTexCoordPointer / glDrawArrays, sans
// une ligne de Cg. C'est exactement le style de src/hardware/r_opengl/r_opengl.c
// (0 occurrence de glBegin, tout en tableaux de sommets). L'exemple
// hello-psgl-textured-quad, lui, passe par des shaders Cg : il ne nous
// renseignerait pas sur le chemin dont on a besoin.
//
// Deux ecarts volontaires par rapport a l'exemple :
//  - texture generee au lieu d'un PNG decode, pour ne pas dependre de pngdec ;
//  - psglCreateDeviceExtended plutot que psglCreateDeviceAuto, parce que la
//    resolution EST la question.
//
// ATTENTION A LA LECTURE DES CHIFFRES SOUS RPCS3. L'emulateur rend avec le GPU
// de l'hote (une RTX 3060 ici) et ne modelise ni les 256 Mo ni le taux de
// remplissage du RSX. Sous emulateur ce programme prouve que le CHEMIN marche ;
// il ne dit rien du cout reel. Le chiffre d'A3 ne vaudra que sur console.

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <PSGL/psgl.h>
#include <ppu-lv2.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/process.h>
#include <sys/systime.h>
#include <sys/timer.h>

SYS_PROCESS_PARAM(1001, 0x100000);

#define OUT_PATH "/dev_hdd0/game/SRBK00001/USRDIR/psgl_probe.txt"

#define WANT_W 1280u
#define WANT_H 720u

#define TEX_SIZE 64
#define MAX_QUADS 2048

typedef struct { float x, y, z, u, v; } Vertex;

static Vertex g_verts[MAX_QUADS * 6];
static uint8_t g_tex[TEX_SIZE * TEX_SIZE * 4];

// Journal : fichier + stdout. Le fichier est ce qu'on lit apres coup, stdout
// atterrit dans RPCS3.log.
static void say(const char *fmt, ...)
{
	va_list ap;
	char line[256];
	FILE *f;

	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);

	printf("[psgl_probe] %s\n", line);

	f = fopen(OUT_PATH, "a");
	if (f) { fprintf(f, "%s\n", line); fclose(f); }
}

// Meme recette d'horloge que le jeu (src/sdl/i_system.c) : sysGetCurrentTime
// compose en UN compteur de microsecondes. Garder sec et nsec separes fait
// sous-deborder la soustraction 64 bits au changement de seconde -- c'est le
// bug qui affichait 48 jours de temps de frame chez TanakaDOOM-cGcm.
static uint64_t now_us(void)
{
	uint64_t sec = 0, nsec = 0;
	sysGetCurrentTime(&sec, &nsec);
	return sec * 1000000ULL + nsec / 1000ULL;
}

static void make_texture(void)
{
	int x, y;
	for (y = 0; y < TEX_SIZE; y++)
		for (x = 0; x < TEX_SIZE; x++)
		{
			uint8_t *p = &g_tex[(y * TEX_SIZE + x) * 4];
			int c = ((x >> 3) ^ (y >> 3)) & 1;
			p[0] = c ? 230 : 40;
			p[1] = c ? 180 : 60;
			p[2] = c ?  60 : 140;
			p[3] = 255;
		}
}

// n quads repartis sur l'ecran. size est en fraction d'ecran : de petits quads
// mesurent le cout PAR APPEL, un quad plein ecran mesure le remplissage.
static void build_quads(int n, float size)
{
	int i;
	for (i = 0; i < n; i++)
	{
		// Disposition en grille, sans recouvrement pour les petits quads.
		int cols = 64;
		float cx = -1.0f + 2.0f * ((float)(i % cols) + 0.5f) / (float)cols;
		float cy = -1.0f + 2.0f * ((float)((i / cols) % cols) + 0.5f) / (float)cols;
		float h = size;
		Vertex *v = &g_verts[i * 6];

		v[0].x = cx - h; v[0].y = cy - h; v[0].u = 0.0f; v[0].v = 1.0f;
		v[1].x = cx + h; v[1].y = cy - h; v[1].u = 1.0f; v[1].v = 1.0f;
		v[2].x = cx - h; v[2].y = cy + h; v[2].u = 0.0f; v[2].v = 0.0f;
		v[3].x = cx - h; v[3].y = cy + h; v[3].u = 0.0f; v[3].v = 0.0f;
		v[4].x = cx + h; v[4].y = cy - h; v[4].u = 1.0f; v[4].v = 1.0f;
		v[5].x = cx + h; v[5].y = cy + h; v[5].u = 1.0f; v[5].v = 0.0f;

		v[0].z = v[1].z = v[2].z = v[3].z = v[4].z = v[5].z = 0.0f;
	}
}

// Une phase de mesure. drawcalls == 0 mesure le plancher (clear + swap seuls),
// ce qui donne le plafond impose par le vsync et permet de savoir si les autres
// phases sont vraiment limitees par le dessin.
static void phase(const char *name, int quads, float size, int frames)
{
	uint64_t t0, t1;
	int i;
	double ms;

	if (quads > MAX_QUADS) quads = MAX_QUADS;
	if (quads > 0)
	{
		build_quads(quads, size);
		glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &g_verts[0].x);
		glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &g_verts[0].u);
	}

	// Deux frames a blanc : la premiere validation d'etat de PSGL est chere et
	// n'a pas a polluer la moyenne.
	for (i = 0; i < 2; i++)
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (quads > 0) glDrawArrays(GL_TRIANGLES, 0, quads * 6);
		psglSwap();
	}

	t0 = now_us();
	for (i = 0; i < frames; i++)
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (quads > 0) glDrawArrays(GL_TRIANGLES, 0, quads * 6);
		psglSwap();
	}
	t1 = now_us();

	ms = (double)(t1 - t0) / 1000.0 / (double)frames;
	say("%-22s quads=%-5d  %7.3f ms/frame  %6.1f fps", name, quads, ms,
		ms > 0.0 ? 1000.0 / ms : 0.0);
}

int main(void)
{
	PSGLinitOptions options;
	PSGLdeviceParameters params;
	PSGLdevice *device;
	PSGLcontext *ctx;
	GLuint tex = 0;
	GLuint dw = 0, dh = 0, rw = 0, rh = 0;
	int forced = 1;

	{ FILE *f = fopen(OUT_PATH, "w"); if (f) fclose(f); } // repartir a zero

	say("=== psgl_probe PROBE_BUILD_20260902_151349 ===");

	memset(&options, 0, sizeof options);
	psglInit(&options);
	say("psglInit ok");

	// A2 : imposer 1280x720.
	//
	// CORRIGE le 02/09 15:20. Le premier essai ne posait que WIDTH_HEIGHT et
	// obtenait device=1280x720 mais renderbuffer=1920x1080. La lecture de
	// psgl_context.c:2509-2521 explique pourquoi, et les deux champs ne sont pas
	// interchangeables :
	//
	//   WIDTH_HEIGHT             -> device->width/height, la taille de SORTIE
	//   RESC_RENDER_WIDTH_HEIGHT -> device->render_width/height, ce qu'on REND
	//
	// Sans le second, render_* garde la resolution du mode video detecte. Il
	// faut donc les deux -- et cette separation est exactement le levier du mode
	// performance : rendre petit, laisser RESC monter a l'echelle pour la
	// sortie, ce que fait le portage Vita (640x368 rendu, 960x544 affiche).
	memset(&params, 0, sizeof params);
	params.enable = PSGL_DEVICE_PARAMETERS_WIDTH_HEIGHT
	              | PSGL_DEVICE_PARAMETERS_RESC_RENDER_WIDTH_HEIGHT;
	params.width = WANT_W;
	params.height = WANT_H;
	params.renderWidth = WANT_W;
	params.renderHeight = WANT_H;

	device = psglCreateDeviceExtended(&params);
	if (!device)
	{
		say("psglCreateDeviceExtended(%ux%u) a ECHOUE -- repli sur Auto",
			WANT_W, WANT_H);
		device = psglCreateDeviceAuto(0, 0, 0);
		forced = 0;
	}

	ctx = psglGetCurrentContext();
	if (!ctx) ctx = psglCreateContext();

	if (!device || !ctx)
	{
		say("ECHEC PSGL : device=%p context=%p", (void *)device, (void *)ctx);
		psglExit();
		return 1;
	}

	psglMakeCurrent(ctx, device);

	psglGetDeviceDimensions(device, &dw, &dh);
	psglGetRenderBufferDimensions(device, &rw, &rh);
	say("device      : %ux%u  (%s)", dw, dh, forced ? "impose" : "auto");
	say("renderbuffer: %ux%u", rw, rh);
	say("aspect      : %.4f", (double)psglGetDeviceAspectRatio(device));
	say("GL_VENDOR   : %s", (const char *)glGetString(GL_VENDOR));
	say("GL_RENDERER : %s", (const char *)glGetString(GL_RENDERER));
	say("GL_VERSION  : %s", (const char *)glGetString(GL_VERSION));

	if (rw != WANT_W || rh != WANT_H)
		say("NOTE: la resolution obtenue n'est pas celle demandee.");

	// --- pipeline FIXE, exactement comme r_opengl.c ---
	make_texture();
	glGenTextures(1, &tex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, g_tex);
	glEnable(GL_TEXTURE_2D);
	say("texture %dx%d RGBA chargee, glGetError=%d", TEX_SIZE, TEX_SIZE,
		(int)glGetError());

	// Tableaux de sommets cote client -- pas de VBO. C'est ce que fait
	// r_opengl.c pour les murs et les sprites, et le cas qui nous interesse.
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glViewport(0, 0, (GLsizei)rw, (GLsizei)rh);
	glClearColor(0.02f, 0.08f, 0.18f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_SCISSOR_TEST);
	say("etat fixed-function pose, glGetError=%d", (int)glGetError());

	// --- A3 : le cout ---
	say("--- mesures (%ux%u) ---", rw, rh);
	phase("plancher clear+swap",     0,    0.00f, 120);
	phase("100 petits quads",      100,    0.012f, 120);
	phase("500 petits quads",      500,    0.012f, 120);
	phase("2000 petits quads",    2000,    0.012f, 120);
	phase("1 quad plein ecran",      1,    1.00f, 120);
	phase("8 quads plein ecran",     8,    1.00f, 120);
	phase("32 quads plein ecran",   32,    1.00f, 120);

	say("glGetError final=%d", (int)glGetError());
	say("=== fin ===");

	// LE TEST QUI TRANCHE : est-ce que PSGL DESSINE ?
	//
	// On ne peut pas le verifier par relecture -- glReadPixels de PSGL est
	// inerte, il se contente de zeroter le tampon qu'on lui donne
	// (core_gl.c:497-503). Le seul juge est donc l'ecran.
	//
	// D'ou une boucle longue, et un motif qu'on ne peut pas confondre avec un
	// simple glClear : fond BLEU SOMBRE, quads en damier ORANGE et VIOLET
	// disposes en grille. Si la capture montre du bleu uni, glClear marche mais
	// glDrawArrays ne dessine pas -- et on bascule sur la methode IoQuake3.
	say("--- boucle visible : 90 s, 64 quads sur fond bleu ---");
	say("(si l'ecran est bleu UNI, PSGL ne dessine pas)");
	{
		int i;
		build_quads(64, 0.10f);
		glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &g_verts[0].x);
		glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &g_verts[0].u);
		for (i = 0; i < 5400; i++)
		{
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glDrawArrays(GL_TRIANGLES, 0, 64 * 6);
			psglSwap();
			sys_timer_usleep(16000);
		}
	}

	psglExit();
	return 0;
}
