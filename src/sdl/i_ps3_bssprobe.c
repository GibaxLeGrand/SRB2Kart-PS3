// SONIC ROBO BLAST 2 KART -- PS3 port
//-----------------------------------------------------------------------------
/// \file  i_ps3_bssprobe.c
/// \brief 16 Mo de BSS, pour isoler une des deux differences du build OpenGL.
///
/// 2026-09-05 -- bissection du non-demarrage sur console reelle.
///
/// Constat : le build LOGICIEL demarre sur la PS3 (56 s, 60 fps, ecran-titre),
/// le build OPENGL n'ecrit rien du tout -- pas meme son constructeur statique,
/// qui est pourtant le dernier des quatre de .ctors. Retirer l'import PRX
/// sys_fs (paquet B du 05/09) n'a rien change.
///
/// Il reste deux differences entre les deux binaires :
///
///   1. le code de PSGL et des 12 objets hw_*/r_opengl est present ;
///   2. le BSS passe de 2,4 Mo a 19,3 Mo, dont 16 Mo pour le seul
///      `static RGBA_t tex[2048*2048]` de hardware/r_opengl/r_opengl.c:1552.
///
/// libPSGL.a n'a aucun BSS propre (mesure au nm), donc les deux differences se
/// separent proprement. Ce fichier ajoute UNIQUEMENT la deuxieme a un build
/// logiciel connu bon : meme taille, meme section, pas une ligne de PSGL.
///
/// Si le binaire ainsi produit ne demarre plus, c'est la taille du BSS qui tue
/// le chargement, et le correctif est d'allouer `tex` dynamiquement.
/// S'il demarre, la taille du BSS est hors de cause.
///
/// A n'activer que via PS3_BSS_PROBE=1 (voir sdl/Makefile.cfg). Ce n'est pas du
/// code de production : c'est une mesure, et elle doit disparaitre ensuite.
//-----------------------------------------------------------------------------

#ifdef _PS3

#include <stddef.h>

size_t ps3_bss_probe_size(void);

// Global non statique : ni le compilateur ni le lieur ne peuvent l'ecarter, et
// l'objet est cite explicitement dans la ligne de liens. 16 Mo exactement, la
// meme taille que tex[2048*2048] avec RGBA_t sur 4 octets.
char ps3_bss_probe[16u * 1024u * 1024u];

size_t ps3_bss_probe_size(void)
{
	return sizeof(ps3_bss_probe);
}

#endif // _PS3
