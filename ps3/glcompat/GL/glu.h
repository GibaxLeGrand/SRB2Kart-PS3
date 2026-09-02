/* ps3/glcompat/GL/glu.h -- 2026-09-02
 *
 * r_opengl.h inclut <GL/glu.h> inconditionnellement hors Windows/macOS, mais
 * le moteur n'utilise de GLU qu'UNE fonction : gluBuild2DMipmaps.
 *
 * PS3DK fournit libPSGLU.a, qui l'exporte -- verifie avec nm plutot que
 * suppose. On se contente donc de la declarer. */

#ifndef PS3_GLCOMPAT_GLU_H
#define PS3_GLCOMPAT_GLU_H

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat,
	GLsizei width, GLsizei height,
	GLenum format, GLenum type, const void *data);

#ifdef __cplusplus
}
#endif

#endif /* PS3_GLCOMPAT_GLU_H */
