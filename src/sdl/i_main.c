// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief Main program, simply calls D_SRB2Main and D_SRB2Loop, the high level loop.

#include "../doomdef.h"
#include "../m_argv.h"
#include "../d_main.h"
#include "../i_system.h"
#ifdef _PS3
#include "../screen.h" // PS3_RENDER_W/H, stamped into the boot marker below
#endif

#ifdef _PS3
#include <sys/process.h>

// 2026-08-27: declare the primary PPU thread's priority and stack size.
//
// Without this the process takes lv2's default primary stack, which is far
// smaller than the 1MB RPCS3 hands out -- our own probe reports
// "stack size=1048576" under the emulator, which is why nothing ever showed
// up there.  On real hardware the game froze inside P_BackupTables(), and the
// reason is that lzf_compress() puts its hash table ON THE STACK:
//
//     #define HLOG 15
//     typedef const u8 *LZF_STATE[1 << (HLOG)];   // 32768 pointers
//     #define LZF_STATE_ARG 0                     // stack-allocated
//
// That is 128KB in one frame with 32-bit PPU pointers.  IoQuake3-PS3 declares
// SYS_PROCESS_PARAM(1001, 0x100000) for the same reason; PS3DK's own samples
// use the macro too, and lv2.ld already places the .sys_proc_param section.
SYS_PROCESS_PARAM(1001, 0x100000);
#endif

#ifdef __GNUC__
#include <unistd.h>
#endif

#ifdef HAVE_SDL

#ifdef HAVE_TTF
#include "SDL.h"
#include "i_ttf.h"
#endif

#if defined (_WIN32) && !defined (main)
//#define SDLMAIN
#endif

#ifdef SDLMAIN
#include "SDL_main.h"
#elif defined(FORCESDLMAIN)
extern int SDL_main(int argc, char *argv[]);
#endif

#ifdef LOGMESSAGES
FILE *logstream = NULL;
char  logfilename[1024];
#endif

#ifndef DOXYGEN
#ifndef O_TEXT
#define O_TEXT 0
#endif

#ifndef O_SEQUENTIAL
#define O_SEQUENTIAL 0
#endif
#endif

#ifdef _WIN32
#ifndef _AMD64_
#include "exchndl.h"
#define DRMINGW
#endif
#endif

#if defined (_WIN32)
#include "../win32/win_dbg.h"
typedef BOOL (WINAPI *p_IsDebuggerPresent)(VOID);
#endif

#if defined (_WIN32)
static inline VOID MakeCodeWritable(VOID)
{
#ifdef USEASM // Disable write-protection of code segment
	DWORD OldRights;
	const DWORD NewRights = PAGE_EXECUTE_READWRITE;
	PBYTE pBaseOfImage = (PBYTE)GetModuleHandle(NULL);
	PIMAGE_DOS_HEADER dosH =(PIMAGE_DOS_HEADER)pBaseOfImage;
	PIMAGE_NT_HEADERS ntH = (PIMAGE_NT_HEADERS)(pBaseOfImage + dosH->e_lfanew);
	PIMAGE_OPTIONAL_HEADER oH = (PIMAGE_OPTIONAL_HEADER)
		((PBYTE)ntH + sizeof (IMAGE_NT_SIGNATURE) + sizeof (IMAGE_FILE_HEADER));
	LPVOID pA = pBaseOfImage+oH->BaseOfCode;
	SIZE_T pS = oH->SizeOfCode;
#if 1 // try to find the text section
	PIMAGE_SECTION_HEADER ntS = IMAGE_FIRST_SECTION (ntH);
	WORD s;
	for (s = 0; s < ntH->FileHeader.NumberOfSections; s++)
	{
		if (memcmp (ntS[s].Name, ".text\0\0", 8) == 0)
		{
			pA = pBaseOfImage+ntS[s].VirtualAddress;
			pS = ntS[s].Misc.VirtualSize;
			break;
		}
	}
#endif

	if (!VirtualProtect(pA,pS,NewRights,&OldRights))
		I_Error("Could not make code writable\n");
#endif
}
#endif


#ifdef _WIN32
static void
ChDirToExe (void)
{
	CHAR path[MAX_PATH];
	if (GetModuleFileNameA(NULL, path, MAX_PATH) > 0)
	{
		strrchr(path, '\\')[0] = '\0';
		SetCurrentDirectoryA(path);
	}
}
#endif


/**	\brief	The main function

	\param	argc	number of arg
	\param	*argv	string table

	\return	int
*/
#if defined (__GNUC__) && (__GNUC__ >= 4)
#pragma GCC diagnostic ignored "-Wmissing-noreturn"
#endif

#ifdef _PS3
// 2026-09-05 -- sonde de demarrage, deux etages.
//
// Le test sur console reelle du 05/09 n'a rien rapporte pour le build OpenGL :
// aucun journal, pas meme "M1 before D_SRB2Main". Impossible de dire si le
// processus mourait au chargement du SELF, dans l'init C, ou dans main().
// Deux raisons a cette cecite, corrigees ici :
//
//   1. psdebug_boot.txt etait ouvert en "w". Le lancement suivant (le temoin
//      logiciel) ecrasait donc la trace OpenGL. -> mode "a".
//   2. Rien ne marquait le pre-main. -> ps3_boot_ctor(), place dans .ctors
//      (ce toolchain utilise .ctors, pas .init_array : verifie au readelf).
//
// Lecture : C0 sans B1 => l'init C tourne, main() n'est pas atteint.
//           ni C0 ni B1 => le loader a refuse le SELF.
// Chaque ligne porte __DATE__/__TIME__ et le renderer, pour qu'on sache
// toujours quel binaire a produit quelle trace.
#ifdef HWRENDER
#define PS3_BOOT_RENDERER "opengl"
#else
#define PS3_BOOT_RENDERER "software"
#endif

static void ps3_bootmark(const char *what)
{
	// Chemin absolu : srb2home vaut encore "." a ce stade, et le repertoire
	// courant n'est pas USRDIR quand on est lance depuis le XMB.
	FILE *f = fopen(PS3_INSTALLDIR "/psdebug_boot.txt", "a");
	if (f)
	{
		fprintf(f, "%s [%s, bati %s %s]\n", what, PS3_BOOT_RENDERER, __DATE__, __TIME__);
		fflush(f);
		fclose(f);
	}
}

static void ps3_boot_ctor(void) __attribute__((constructor));
static void ps3_boot_ctor(void)
{
	ps3_bootmark("C0 constructeur statique (pre-main)");
}
#endif // _PS3

#ifdef FORCESDLMAIN
int SDL_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
	const char *logdir = NULL;
	myargc = argc;
	myargv = argv; /// \todo pull out path to exe from this string

#ifdef _PS3
	// 2026-08-26 -- deliberately the first thing that touches the file system.
	// The first hardware attempt left no psdebug file at all, so there was no
	// way to tell whether main() had even been reached. This writes to an
	// absolute path rather than through srb2home (still "." this early) or the
	// working directory (not USRDIR when launched from the XMB), so if the
	// file is missing the process died before entering main.
	{
		FILE *ps3boot = fopen(PS3_INSTALLDIR "/psdebug_boot.txt", "a");  // 2026-09-05 : "w" effacait la trace du lancement precedent
		if (ps3boot)
		{
			fprintf(ps3boot, "B1 main() reached, render %dx%d, built %s %s, argc=%d, argv0=%s\n",
				PS3_RENDER_W, PS3_RENDER_H, __DATE__, __TIME__,
				argc, (argc > 0 && argv && argv[0]) ? argv[0] : "(null)");
			fflush(ps3boot);
			fclose(ps3boot);
		}
	}
#endif

#ifdef HAVE_TTF
#ifdef _WIN32
	I_StartupTTF(FONTPOINTSIZE, SDL_INIT_VIDEO|SDL_INIT_AUDIO, SDL_SWSURFACE);
#else
	I_StartupTTF(FONTPOINTSIZE, SDL_INIT_VIDEO, SDL_SWSURFACE);
#endif
#endif

#ifdef _WIN32
	ChDirToExe();
#endif

	logdir = D_Home();
#ifdef _PS3
	ps3_bootmark("B2 apres D_Home");
#endif

#ifdef LOGMESSAGES
#ifdef DEFAULTDIR
	if (logdir)
		strcpy(logfilename, va("%s/"DEFAULTDIR"/log.txt",logdir));
	else
#endif
		strcpy(logfilename, "./log.txt");

	logstream = fopen(logfilename, "wt");
#endif

	//I_OutputMsg("I_StartupSystem() ...\n");
	I_StartupSystem();
#ifdef _PS3
	ps3_bootmark("B3 apres I_StartupSystem");
#endif
#if defined (_WIN32)
	{
#if 0 // just load the DLL
		p_IsDebuggerPresent pfnIsDebuggerPresent = (p_IsDebuggerPresent)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsDebuggerPresent");
		if ((!pfnIsDebuggerPresent || !pfnIsDebuggerPresent())
#ifdef BUGTRAP
			&& !InitBugTrap()
#endif
			)
#endif
		{
#ifdef DRMINGW
			ExcHndlInit();
#endif
		}
	}
#ifndef __MINGW32__
	prevExceptionFilter = SetUnhandledExceptionFilter(RecordExceptionInfo);
#endif
	MakeCodeWritable();
#endif

	// startup SRB2
	CONS_Printf("Setting up SRB2Kart...\n");
#ifdef _PS3
	{
		FILE *ps3mf = fopen(PS3_DebugPath("psdebug6.txt"), "a");
		if (ps3mf) { fputs("M1 before D_SRB2Main", ps3mf); fputc('\n', ps3mf); fflush(ps3mf); fclose(ps3mf); }
	}
#endif
	D_SRB2Main();
#ifdef _PS3
	{
		FILE *ps3mf = fopen(PS3_DebugPath("psdebug6.txt"), "a");
		if (ps3mf) { fputs("M2 after D_SRB2Main, before D_SRB2Loop", ps3mf); fputc('\n', ps3mf); fflush(ps3mf); fclose(ps3mf); }
	}
#endif
	CONS_Printf("Entering main game loop...\n");
	// never return
	D_SRB2Loop();

#ifdef BUGTRAP
	// This is safe even if BT didn't start.
	ShutdownBugTrap();
#endif

	// return to OS
	return 0;
}
#endif
