/* ps3/glcompat/GL/gl.h -- 2026-09-02
 *
 * Passerelle entre ce que src/hardware/r_opengl/r_opengl.c demande -- du
 * OpenGL 1.1 de bureau, via <GL/gl.h> -- et ce que PS3DK fournit : PSGL, qui
 * expose du OpenGL ES 1.1 sous <GLES/gl.h>.
 *
 * Ce fichier n'est PAS une couche de traduction. PSGL EST l'implementation :
 * verifie a l'execution le 02/09/2026 (voir ps3/psgl_probe/main.c et
 * AUDIT_VITA_20260902.md §1bis) -- il dessine des quads textures en pipeline
 * fixe, en 1280x720, sans un shader ecrit par nous. Sur les 82 points d'entree
 * GL que r_opengl.c appelle, PSGL en fournit 75. Il ne manque donc que les
 * SEPT ci-dessous, et chacun a un equivalent GLES d'une ligne.
 *
 * C'est ce qui nous separe d'IoQuake3-PS3, qui a du ecrire 2903 lignes de
 * GL->cellGcm a la main : leur toolchain (PSL1GHT) n'a pas PSGL, la notre si.
 *
 * SRB2Kart se prete bien a l'exercice, ce qui n'allait pas de soi :
 *   - AUCUN mode immediat -- 0 occurrence de glBegin/glEnd/glVertex3f, tout
 *     passe par des tableaux de sommets, comme GLES l'exige ;
 *   - AUCUN GL_QUADS ni GL_POLYGON -- uniquement GL_TRIANGLES,
 *     GL_TRIANGLE_FAN et GL_TRIANGLE_STRIP, que GLES 1.1 connait tous. Il n'y
 *     a donc aucune geometrie a convertir.
 *
 * Reserve honnete : reste `glTexEnvi`, que PSGL declare et exporte mais dont
 * le corps ne fait rien. Les modes d'environnement de texture seront ignores
 * tant qu'on ne l'aura pas complete -- c'est la, et seulement la, que la
 * conception d'IoQuake3-PS3 (leurs cles de shader PS3GL_TENV_*) nous servira.
 */

#ifndef PS3_GLCOMPAT_GL_H
#define PS3_GLCOMPAT_GL_H

#include <GLES/gl.h>
#include <GLES/glext.h>

/* Le moteur decore ses pointeurs de fonction avec APIENTRY. GLES ne le
 * definit pas : sur cette ABI, rien a decorer. */
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPIENTRY
#define GLAPIENTRY APIENTRY
#endif

/* GLES 1.1 est entierement en simple precision : pas de GLdouble, pas de
 * GLclampd. r_opengl.c n'en a qu'un usage. */
#ifndef GL_VERSION_1_1
typedef double GLdouble;
typedef double GLclampd;
#endif

/* GLES n'a que GL_CLAMP_TO_EDGE. Un seul usage cote moteur, et la difference
 * (GL_CLAMP echantillonne la bordure, CLAMP_TO_EDGE le dernier texel) est sans
 * consequence ici : aucune couleur de bordure n'est jamais posee. */
#ifndef GL_CLAMP
#define GL_CLAMP GL_CLAMP_TO_EDGE
#endif

/* GL_VIEWPORT : le moteur le lit deux fois via glGetIntegerv (SetModelView et
 * GClipRect). Les en-tetes GLES de PS3DK ne le declarent pas ; la valeur
 * ci-dessous est celle d'OpenGL depuis la 1.0.
 *
 * ⚠ MAIS PSGL NE LE TRAITE PAS. Verifie, apres avoir d'abord ecrit ici le
 * contraire : le glGetIntegerv de core_gl.c:56 est un switch sur une liste
 * fixe -- GL_MAX_LIGHTS, GL_MAX_TEXTURE_SIZE, GL_MAX_VIEWPORT_DIMS,
 * GL_RED_BITS, GL_FOG_MODE... -- et GL_VIEWPORT n'y figure pas. Le define
 * suffit donc a COMPILER, pas a obtenir la bonne valeur : le tableau passe
 * par le moteur ne sera pas rempli.
 *
 * C'est le deuxieme trou fonctionnel de PSGL pour nous, avec glTexEnvi. Le
 * remede est simple quand on y viendra -- on connait le viewport, c'est nous
 * qui l'avons pose -- mais il n'est pas fait ici. */
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif

/* ------------------------------------------------------------------------
 * LES SEPT MANQUANTS.
 *
 * `static inline` plutot que de vraies fonctions : la traduction disparait a
 * la compilation, et rien n'est ajoute a l'edition de liens.
 * ------------------------------------------------------------------------ */

/* 1-2. Profondeur : GLES ne prend que la variante flottante.
 *
 * ATTENTION, mesure et non supposee : le glDepthRangef de PSGL est un no-op
 * (core_gl.c:151-152, il jette ses deux arguments). Ce shim compile et lie,
 * mais l'intervalle de profondeur ne sera PAS applique. Sans consequence tant
 * que le moteur s'en tient a 0..1, ce qu'il fait -- a revoir si un jour il
 * s'en ecarte. */
static inline void ps3gl_ClearDepth(GLclampd d) { glClearDepthf((GLclampf)d); }
static inline void ps3gl_DepthRange(GLclampd n, GLclampd f)
{
	glDepthRangef((GLclampf)n, (GLclampf)f);
}
#define glClearDepth ps3gl_ClearDepth
#define glDepthRange ps3gl_DepthRange

/* 3. Couleur par vecteur d'octets -> la forme scalaire. */
static inline void ps3gl_Color4ubv(const GLubyte *v)
{
	glColor4ub(v[0], v[1], v[2], v[3]);
}
#define glColor4ubv ps3gl_Color4ubv

/* 4. Materiau entier -> flottant. Le seul parametre entier que le moteur passe
 * est GL_SHININESS, dont la conversion est exacte. */
static inline void ps3gl_Materiali(GLenum face, GLenum pname, GLint param)
{
	glMaterialf(face, pname, (GLfloat)param);
}
#define glMateriali ps3gl_Materiali

/* 5. Matrice double precision -> simple.
 *
 * Recopie explicite des 16 elements : un cast de pointeur donnerait des
 * ordures, les deux types n'ayant ni la meme taille ni la meme representation. */
static inline void ps3gl_MultMatrixd(const GLdouble *m)
{
	GLfloat f[16];
	int i;
	for (i = 0; i < 16; i++)
		f[i] = (GLfloat)m[i];
	glMultMatrixf(f);
}
#define glMultMatrixd ps3gl_MultMatrixd

/* 6-7. Coordonnees de texture multi-unites.
 *
 * GLES 1.1 n'a que la forme a quatre composantes. Les valeurs par defaut d'un
 * texcoord OpenGL sont (s, t, 0, 1), donc on complete avec r=0 et q=1. */
static inline void ps3gl_MultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	glMultiTexCoord4f(target, s, t, 0.0f, 1.0f);
}
static inline void ps3gl_MultiTexCoord2fv(GLenum target, const GLfloat *v)
{
	glMultiTexCoord4f(target, v[0], v[1], 0.0f, 1.0f);
}
#define glMultiTexCoord2f  ps3gl_MultiTexCoord2f
#define glMultiTexCoord2fv ps3gl_MultiTexCoord2fv

#endif /* PS3_GLCOMPAT_GL_H */
