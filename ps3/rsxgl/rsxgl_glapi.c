// SONIC ROBO BLAST 2 KART -- portage PS3
//-----------------------------------------------------------------------------
/// ile  rsxgl_glapi.c
/// rief Passerelle gl* -> ps3gl_*, au-dessus de la couche RSX d'IoQuake3.
///
/// 2026-09-06 -- premiere pierre de la fondation qui remplace PSGL.
///
/// SRB2 appelle OpenGL par des entrees gl* (hardware/r_opengl/r_opengl.c, sous
/// STATIC_OPENGL elles se resolvent a l'edition de liens). La couche reprise
/// d'IoQuake3-PS3 expose les siennes sous le prefixe ps3gl_. Ce fichier fait la
/// jonction, et c'est tout : aucune logique de rendu ici.
///
/// Perimetre mesure le 06/09 :
///
///   81  entrees gl* utilisees par r_opengl.c
///  -20  derriere #ifdef GL_SHADERS, non defini dans le build PS3
///  ---
///   61  a fournir
///  -47  deja fournies par IoQuake3 (suffixes ARB/EXT normalises)
///  ---
///   14  a ecrire, dont UNE SEULE de vrai travail : glCopyTexImage2D
///
/// Les 47 enveloppes ci-dessous sont generees a partir des prototypes reels de
/// ps3gl.h, pas recopiees a la main : c'est ce qui garantit que les signatures
/// concordent.
//-----------------------------------------------------------------------------

#ifdef _PS3

#include <stddef.h> // ptrdiff_t, sur lequel GLsizeiptr est bati

#include "GL/gl.h"
#include "ps3gl.h"

// ==========================================================================
//                          LES 47 DEJA FOURNIES
// ==========================================================================

void glActiveTexture(GLenum texture)
{
	ps3gl_ActiveTextureARB(texture);
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
	ps3gl_AlphaFunc(func, ref);
}

void glBindTexture(GLenum target, GLuint texture)
{
	ps3gl_BindTexture(target, texture);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	ps3gl_BlendFunc(sfactor, dfactor);
}

void glClear(GLbitfield mask)
{
	ps3gl_Clear(mask);
}

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
	ps3gl_ClearColor(r, g, b, a);
}

void glClearDepth(GLclampd depth)
{
	ps3gl_ClearDepth(depth);
}

void glClientActiveTexture(GLenum texture)
{
	ps3gl_ClientActiveTextureARB(texture);
}

void glColor4ubv(const GLubyte *v)
{
	ps3gl_Color4ubv(v);
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	ps3gl_ColorMask(r, g, b, a);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	ps3gl_ColorPointer(size, type, stride, ptr);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h)
{
	ps3gl_CopyTexSubImage2D(target, level, xoff, yoff, x, y, w, h);
}

void glCullFace(GLenum mode)
{
	ps3gl_CullFace(mode);
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
	ps3gl_DeleteTextures(n, textures);
}

void glDepthFunc(GLenum func)
{
	ps3gl_DepthFunc(func);
}

void glDepthMask(GLboolean flag)
{
	ps3gl_DepthMask(flag);
}

void glDepthRange(GLclampd n, GLclampd f)
{
	ps3gl_DepthRange(n, f);
}

void glDisable(GLenum cap)
{
	ps3gl_Disable(cap);
}

void glDisableClientState(GLenum cap)
{
	ps3gl_DisableClientState(cap);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	ps3gl_DrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	ps3gl_DrawElements(mode, count, type, indices);
}

void glEnable(GLenum cap)
{
	ps3gl_Enable(cap);
}

void glEnableClientState(GLenum cap)
{
	ps3gl_EnableClientState(cap);
}

void glGenTextures(GLsizei n, GLuint *textures)
{
	ps3gl_GenTextures(n, textures);
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
	ps3gl_GetFloatv(pname, params);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
	ps3gl_GetIntegerv(pname, params);
}

const GLubyte * glGetString(GLenum name)
{
	return ps3gl_GetString(name);
}

void glLoadIdentity(void)
{
	ps3gl_LoadIdentity();
}

void glMatrixMode(GLenum mode)
{
	ps3gl_MatrixMode(mode);
}

void glMultMatrixf(const GLfloat *m)
{
	ps3gl_MultMatrixf(m);
}

void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
	ps3gl_MultiTexCoord2fARB(target, s, t);
}

void glPixelStorei(GLenum pname, GLint param)
{
	ps3gl_PixelStorei(pname, param);
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
	ps3gl_PolygonOffset(factor, units);
}

void glPopMatrix(void)
{
	ps3gl_PopMatrix();
}

void glPushMatrix(void)
{
	ps3gl_PushMatrix();
}

void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)
{
	ps3gl_ReadPixels(x, y, w, h, format, type, pixels);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	ps3gl_Rotatef(angle, x, y, z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	ps3gl_Scalef(x, y, z);
}

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
	ps3gl_Scissor(x, y, w, h);
}

void glShadeModel(GLenum mode)
{
	ps3gl_ShadeModel(mode);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	ps3gl_TexCoordPointer(size, type, stride, ptr);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	ps3gl_TexEnvi(target, pname, param);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
	ps3gl_TexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	ps3gl_TexParameteri(target, pname, param);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	ps3gl_Translatef(x, y, z);
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	ps3gl_VertexPointer(size, type, stride, ptr);
}

void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
	ps3gl_Viewport(x, y, w, h);
}

// ==========================================================================
//                          LES 14 RESTANTES
// ==========================================================================

// --- Tampons de sommets : neutralises ---------------------------------------
//
// SRB2 cree un VBO par frame de modele (CreateModelVBOs). Le portage Vita a
// rendu ces appels inertes et dessine depuis des tableaux client -- meme
// strategie ici, d'autant que les modeles sont hors perimetre tant que le
// rendu de base n'affiche pas. Renvoyer des noms nuls est deliberе : le moteur
// verifie vboID avant de s'en servir.

void glGenBuffers(GLsizei n, GLuint *buffers)
{
	GLsizei i;
	for (i = 0; i < n; i++)
		buffers[i] = 0;
}

void glBindBuffer(GLenum target, GLuint buffer)
{
	(void)target; (void)buffer;
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
	(void)target; (void)size; (void)data; (void)usage;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
	(void)n; (void)buffers;
}

// --- Alias de type ----------------------------------------------------------

void glMultMatrixd(const GLdouble *m)
{
	GLfloat f[16];
	int i;
	for (i = 0; i < 16; i++)
		f[i] = (GLfloat)m[i];
	ps3gl_MultMatrixf(f);
}

void glMultiTexCoord2fv(GLenum target, const GLfloat *v)
{
	ps3gl_MultiTexCoord2fARB(target, v[0], v[1]);
}

void glMateriali(GLenum face, GLenum pname, GLint param)
{
	(void)face; (void)pname; (void)param;
}

// --- Brouillard : a implementer --------------------------------------------
//
// SRB2 s'en sert pour la brume de distance. Sans lui la scene s'affiche, elle
// est seulement moins jolie -- donc ce n'est pas bloquant pour le premier
// jalon.

void glFogf(GLenum pname, GLfloat param)
{
	(void)pname; (void)param;
}

void glFogfv(GLenum pname, const GLfloat *params)
{
	(void)pname; (void)params;
}

// --- Eclairage des modeles : hors perimetre pour l'instant ------------------

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
	(void)light; (void)pname; (void)params;
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
	(void)pname; (void)params;
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
	(void)face; (void)pname; (void)params;
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
	(void)type; (void)stride; (void)ptr;
}

// --- LE morceau : copie du tampon de couleur --------------------------------
//
// HWR_MakeScreenFinalTexture() copie le tampon arriere dans une texture, puis
// HWR_DrawScreenFinalTexture() la redessine plein ecran. C'est le mecanisme des
// wipes et des fondus.
//
// C'est exactement ce que PSGL ne savait pas faire -- son glCopyTexImage2D ne
// copiait que la profondeur -- et c'est aussi le blocage n°4 du portage Vita,
// ou l'appel est un no-op et peint une texture vide par-dessus l'image finie :
// le jeu tourne, le son joue, l'ecran est noir.
//
// Tant qu'il n'est pas implemente, la passe d'ecran final DOIT etre sautee
// cote moteur (c'est deja le cas, voir PS3_USE_FINAL_TEXTURE dans
// sdl/ogl_psgl.c) et les wipes restent desactives. Un no-op silencieux ici
// serait pire que rien.
//
// IoQuake3 fournit ps3gl_CopyTexSubImage2D, qui fait la moitie du chemin : il
// reste a allouer la texture a la bonne taille avant de copier.

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
					  GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
	(void)target; (void)level; (void)internalformat;
	(void)x; (void)y; (void)width; (void)height; (void)border;
	// A FAIRE : allouer la texture puis reutiliser le chemin de
	// ps3gl_CopyTexSubImage2D. Voir PLAN_RSX_20260906.md, etape 6.
}

#endif // _PS3
