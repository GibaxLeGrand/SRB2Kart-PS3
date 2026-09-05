// SONIC ROBO BLAST 2 KART -- PS3 port
//-----------------------------------------------------------------------------
/// \file  i_ps3_cellfs.c
/// \brief Implementations locales de cellFsOpen/Read/Write/Lseek/Close pour PSGL.
///
/// 2026-09-05 -- pourquoi ce fichier existe.
///
/// libPSGL.a reference quatre entrees du module PRX `sys_fs` (cellFsOpen,
/// cellFsRead, cellFsLseek, cellFsClose), et une seule fonction s'en sert :
/// psgl_context_load_shader_library() dans psgl_context.c, qui lit un cache de
/// shaders precompiles. Pour satisfaire ces symboles, sdl/Makefile.cfg liait
/// -lfs_stub.
///
/// Consequence mesuree au readelf le 05/09 : l'EBOOT OpenGL importe le module
/// PRX `sys_fs` (libfs_stub.a ne contient qu'un seul objet, donc lier une seule
/// fonction tire les 59 stubs), alors que le build LOGICIEL ne l'importe pas du
/// tout -- et le build logiciel lit pourtant tous ses WAD sans probleme, parce
/// que le liblv2 de PS3DK passe par des appels systeme et non par le PRX.
///
/// C'est la SEULE difference d'imports PRX entre les deux binaires :
///
///     OpenGL   : cellAudio cellGcmSys cellSysutil sysPrxForUser sys_io sys_fs
///     Logiciel : cellAudio cellGcmSys cellSysutil sysPrxForUser sys_io
///
/// Or la resolution des imports PRX est faite en bloc par le loader au
/// demarrage du processus : un seul NID absent de la firmware et le processus
/// ne demarre pas -- ce qui correspond exactement au symptome observe sur
/// console reelle (ecran noir, aucun journal, pas meme le marqueur pose au tout
/// debut de main()). RPCS3, lui, tolere un import non resolu.
///
/// Ces implementations passent par les appels POSIX de newlib -- le meme chemin
/// que le build logiciel, celui dont on sait qu'il fonctionne sur ta console --
/// et permettent de retirer -lfs_stub, donc de supprimer l'import `sys_fs`.
///
/// Ce n'est pas une preuve : c'est une hypothese, et elle est verifiable au
/// readelf avant meme d'aller sur la console (l'import doit disparaitre).
//-----------------------------------------------------------------------------

#ifdef _PS3

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Les constantes de flags de PSGL (psgl_context.c) sont les valeurs octales
// POSIX : O_RDONLY=0, O_WRONLY=01, O_CREAT=0100, O_TRUNC=01000. Elles passent
// donc telles quelles a open(). CELL_OK vaut 0 ; toute valeur negative est une
// erreur, et PSGL ne teste que "!= 0".
#define PS3_CELL_OK        0
#define PS3_CELL_FS_EIO    (-1)

// Prototypes : ces symboles ne sont declares nulle part chez nous -- c'est
// libPSGL qui les declare de son cote (psgl_context.c) et le lieur qui fait la
// jonction. Les repeter ici tait -Wmissing-prototypes et, surtout, fige la
// signature attendue a cote de l'implementation.
int32_t cellFsOpen(const char *path, int32_t flags, int32_t *fd,
                   const void *arg, uint64_t argsize);
int32_t cellFsRead(int32_t fd, void *ptr, uint64_t len, uint64_t *nread);
int32_t cellFsWrite(int32_t fd, const void *ptr, uint64_t len, uint64_t *nwritten);
int32_t cellFsLseek(int32_t fd, int64_t offset, int32_t whence, uint64_t *pos);
int32_t cellFsClose(int32_t fd);

int32_t cellFsOpen(const char *path, int32_t flags, int32_t *fd,
                   const void *arg, uint64_t argsize)
{
	int h;

	(void)arg;
	(void)argsize;

	if (!path || !fd)
		return PS3_CELL_FS_EIO;

	h = open(path, (int)flags, 0666);
	if (h < 0)
		return PS3_CELL_FS_EIO;

	*fd = (int32_t)h;
	return PS3_CELL_OK;
}

int32_t cellFsRead(int32_t fd, void *ptr, uint64_t len, uint64_t *nread)
{
	ssize_t n;

	if (fd < 0 || !ptr)
		return PS3_CELL_FS_EIO;

	n = read((int)fd, ptr, (size_t)len);
	if (n < 0)
	{
		if (nread)
			*nread = 0u;
		return PS3_CELL_FS_EIO;
	}

	// PSGL compare *nread a la taille demandee, donc une lecture courte doit
	// etre rapportee fidelement plutot que transformee en erreur.
	if (nread)
		*nread = (uint64_t)n;
	return PS3_CELL_OK;
}

int32_t cellFsWrite(int32_t fd, const void *ptr, uint64_t len, uint64_t *nwritten)
{
	ssize_t n;

	if (fd < 0 || !ptr)
		return PS3_CELL_FS_EIO;

	n = write((int)fd, ptr, (size_t)len);
	if (n < 0)
	{
		if (nwritten)
			*nwritten = 0u;
		return PS3_CELL_FS_EIO;
	}

	if (nwritten)
		*nwritten = (uint64_t)n;
	return PS3_CELL_OK;
}

int32_t cellFsLseek(int32_t fd, int64_t offset, int32_t whence, uint64_t *pos)
{
	off_t r;

	if (fd < 0)
		return PS3_CELL_FS_EIO;

	// CELL_FS_SEEK_SET/CUR/END valent 0/1/2, comme SEEK_SET/CUR/END.
	r = lseek((int)fd, (off_t)offset, (int)whence);
	if (r == (off_t)-1)
		return PS3_CELL_FS_EIO;

	if (pos)
		*pos = (uint64_t)r;
	return PS3_CELL_OK;
}

int32_t cellFsClose(int32_t fd)
{
	if (fd < 0)
		return PS3_CELL_FS_EIO;
	return (close((int)fd) == 0) ? PS3_CELL_OK : PS3_CELL_FS_EIO;
}

#endif // _PS3
