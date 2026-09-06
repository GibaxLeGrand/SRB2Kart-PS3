// SONIC ROBO BLAST 2 KART -- portage PS3
//-----------------------------------------------------------------------------
/// \file  rsxgl_glue.c
/// \brief Le peu dont la couche GL d'IoQuake3 a besoin de son hote.
///
/// 2026-09-06 -- la couche vient de IoQuake3-PS3 (code/gl/), qui tourne sur
/// console reelle. Elle n'attend qu'un seul symbole de son hote :
/// ps3_log(). Chez eux il va dans leur journal ; chez nous, dans psdebugR.txt,
/// a cote des autres traces PS3.
///
/// Le chemin SPU (ps3gl_spu.c) n'est pas repris : aucun autre fichier de la
/// couche ne le reference, il est isole.
//-----------------------------------------------------------------------------

#ifdef _PS3

#include <stdio.h>

#include "../../src/d_main.h" // PS3_DebugPath

void ps3_log(const char *msg);

void ps3_log(const char *msg)
{
	FILE *f = fopen(PS3_DebugPath("psdebugR.txt"), "a");
	if (f)
	{
		fputs(msg ? msg : "(null)", f);
		fputc(10, f);
		fflush(f);
		fclose(f);
	}
}

#endif // _PS3
