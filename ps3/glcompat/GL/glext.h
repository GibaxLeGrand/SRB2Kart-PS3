/* ps3/glcompat/GL/glext.h -- 2026-09-02
 *
 * Sous STATIC_OPENGL, r_opengl.h pose GL_GLEXT_PROTOTYPES puis inclut
 * <GL/glext.h> pour les entrees GL 1.3 (multitexturing). PSGL les declare dans
 * <GLES/glext.h>, deja tire par notre GL/gl.h -- il n'y a donc rien a ajouter
 * ici, seulement un fichier a offrir a l'inclusion. */

#ifndef PS3_GLCOMPAT_GLEXT_H
#define PS3_GLCOMPAT_GLEXT_H

#include "gl.h"

#endif /* PS3_GLCOMPAT_GLEXT_H */
