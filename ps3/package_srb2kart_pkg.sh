#!/bin/bash
# Build an installable .pkg for a real PS3 (HEN/CFW).  2026-08-26, made
# portable for the SRB2Kart-PS3 repo on 2026-08-31 (was hardcoded to
# /mnt/d/_PS3_Projects/SRB2Kart -- see git history in the private project
# tree for that version).
#
# package_srb2kart.sh makes the plain .self we run under RPCS3.  This makes the
# XMB-installable package instead: an NPDRM fake-self as USRDIR/EBOOT.BIN, the
# PARAM.SFO, the icon, and all the game data alongside it.
#
# APPID must match PS3_INSTALLDIR in src/doomdef.h.  The game looks for
# srb2.srb in /dev_hdd0/game/$APPID/USRDIR before falling back to /app_home
# (which does not exist when launched from the XMB), and writes its config,
# saves and psdebugS.txt to the same place.  Change one, change the other.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

APPID="SRBK00001"
TITLE="SONIC ROBO BLAST 2 KART"
CONTENTID="UP0001-${APPID}_00-0000000000000000"

BIN="$REPO_ROOT/bin/Release"
STAGE="$REPO_ROOT/pkgstage"
OUT="$REPO_ROOT/srb2kart_ps3.pkg"

# LIGHT=1 : ne mettre que l'EBOOT dans le paquet. Les WAD sont deja installes
# dans /dev_hdd0/game/$APPID/USRDIR, et l installateur PS3 fusionne au lieu
# d effacer : seul EBOOT.BIN est remplace. ~2 Mo au lieu de 495 Mo, ce qui rend
# une iteration sur vraie console supportable. C'est aussi le seul mode que le
# CI peut produire : il n'a pas les WAD (donnees de jeu, pas versionnees --
# voir README.md), donc son .pkg contient l'executable et rien d'autre.
if [ "${LIGHT:-0}" = "1" ]; then
	OUT="$REPO_ROOT/srb2kart_ps3_eboot.pkg"
	STAGE="$REPO_ROOT/pkgstage_eboot"
fi
ICON="$SCRIPT_DIR/ICON0.PNG"
SFOXML="$SCRIPT_DIR/sfo.xml"

if [ -z "$PS3DEV" ] && [ -f "$HOME/PS3DK/scripts/env.sh" ]; then
	# shellcheck disable=SC1091
	source "$HOME/PS3DK/scripts/env.sh"
fi
# env.sh derives PS3DEV from its own location, which does not survive every way
# of sourcing it; fall back to the known install prefix rather than silently
# building paths that start with "/bin".
PS3DEV="${PS3DEV:-$HOME/PS3DK/stage/ps3dev}"
MAKE_SELF_NPDRM="$PS3DEV/bin/make_self_npdrm"
SFO="$PS3DEV/bin/sfo"
PKG="$PS3DEV/bin/pkg"

for t in "$MAKE_SELF_NPDRM" "$SFO" "$PKG"; do
	[ -x "$t" ] || { echo "MANQUE: $t"; exit 1; }
done

if [ ! -f "$BIN/srb2kart_ps3.elf" ]; then
	echo "MANQUE: $BIN/srb2kart_ps3.elf -- lancer build_srb2kart_ps3.sh d'abord"
	exit 1
fi

echo "=== $(date '+%Y-%m-%d %H:%M:%S') -- paquet $TITLE ($APPID) ==="

rm -rf "$STAGE"
mkdir -p "$STAGE/USRDIR"

# EBOOT.BIN: NPDRM self carrying the package's own content ID.
#
# 2026-08-26, second attempt. The first package used "fself -n", which makes a
# fake NPDRM self but embeds no content ID; the console refused it at launch
# with 80010009 (CELL_EPERM -- not allowed to run, as opposed to not found).
# make_self_npdrm takes the content ID, and it must match the one handed to pkg
# below or the check fails the same way.
#
# The tool warns "NPDRM cares about the output file name, do not rename", so it
# writes straight to EBOOT.BIN rather than to a temporary that gets moved.
#
# sprxlinker first, exactly as for the RPCS3 .self, or the PRX imports are
# unresolved.
cp "$BIN/srb2kart_ps3.elf" "$STAGE/eboot.elf"
sprxlinker "$STAGE/eboot.elf"
"$MAKE_SELF_NPDRM" "$STAGE/eboot.elf" "$STAGE/USRDIR/EBOOT.BIN" "$CONTENTID"
rm -f "$STAGE/eboot.elf"
echo "EBOOT.BIN : $(stat -c%s "$STAGE/USRDIR/EBOOT.BIN") octets, NPDRM contentid=$CONTENTID"

# 2026-09-05 -- renderer.txt embarque. Corrige un test materiel fausse.
#
# i_video.c choisit son renderer en LISANT ce fichier (I_StartupGraphics) et,
# s'il est absent, retombe sur render_soft -- "Using default software
# renderer." Le paquet ne l'installait pas : le premier essai sur vraie console
# a donc lance le renderer LOGICIEL avec un binaire construit pour OpenGL, et
# on a cru tester PSGL alors qu'on ne le testait pas du tout.
#
# On l'ecrit explicitement d'apres GLRENDER, la meme variable que celle qui
# pilote build_srb2kart_ps3.sh, pour que paquet et binaire ne puissent pas
# diverger. Le jeu reecrit ce fichier lui-meme quand il change de renderer,
# donc l'embarquer ne fige rien.
if [ -n "${GLRENDER:-}" ]; then
	printf 'opengl\n' > "$STAGE/USRDIR/renderer.txt"
	echo "renderer.txt : opengl"
else
	printf 'software\n' > "$STAGE/USRDIR/renderer.txt"
	echo "renderer.txt : software"
fi

# Game data.  Everything the RPCS3 runs use, so the hardware test does not
# change two things at once.  mdls/ is deliberately left out: those are OpenGL
# models and this build is NOHW, so they would be 17MB of dead weight.
if [ "${LIGHT:-0}" = "1" ]; then
	echo "MODE LEGER : aucune donnee de jeu, seul EBOOT.BIN est embarque."
	echo "  Les WAD doivent DEJA etre installes par un paquet complet."
else
for f in srb2.srb gfx.kart textures.kart chars.kart maps.kart \
         bonuschars.kart music.kart sounds.kart mdls.dat; do
	if [ -f "$BIN/$f" ]; then
		cp "$BIN/$f" "$STAGE/USRDIR/"
	else
		echo "  (absent, ignore: $f)"
	fi
done
fi

if [ -f "$ICON" ]; then
	cp "$ICON" "$STAGE/ICON0.PNG"
	echo "ICON0.PNG : fourni"
else
	echo "ICON0.PNG : ABSENT -- le paquet se construira, la PS3 affichera l'icone par defaut"
fi

# 2026-08-30 -- habillage XMB. Noms et formats releves sur un jeu commercial
# reellement installe (BCES01435), pas devines :
#   ICON0.PNG  320x176    l'icone
#   PIC1.PNG   1920x1080  le fond affiche quand l'entree est selectionnee
#   SND0.AT3   RIFF/WAVE portant de l'ATRAC3, le jingle, en boucle
# Tous a la racine du paquet, a cote de PARAM.SFO.
#
# SND0.AT3 n'est pas genere ici : l'ATRAC3 demande le codec ACM de Sony pilote
# depuis Windows, et il n'existe aucun encodeur en ligne de commande sur cette
# machine. Le fichier est donc repris tel quel s'il est fourni.
PIC1="$SCRIPT_DIR/assets_ps3/BACKGROUND.png"
SND0="$SCRIPT_DIR/assets_ps3/SND0.AT3"

if [ -f "$PIC1" ]; then
	cp "$PIC1" "$STAGE/PIC1.PNG"
	echo "PIC1.PNG  : fourni ($(stat -c%s "$PIC1") octets)"
else
	echo "PIC1.PNG  : absent, le XMB gardera son fond par defaut"
fi

if [ -f "$SND0" ]; then
	cp "$SND0" "$STAGE/SND0.AT3"
	echo "SND0.AT3  : fourni ($(stat -c%s "$SND0") octets)"
else
	echo "SND0.AT3  : absent, pas de jingle sur l'icone"
fi

# Both tools want their long options with '=', not a space.
"$SFO" --title="$TITLE" --appid="$APPID" --fromxml "$SFOXML" "$STAGE/PARAM.SFO"
echo "PARAM.SFO : $(stat -c%s "$STAGE/PARAM.SFO") octets"
"$SFO" --list "$STAGE/PARAM.SFO" --pretty | head -20

"$PKG" --contentid="$CONTENTID" "$STAGE/" "$OUT"

echo
echo "=== paquet ecrit ==="
ls -la "$OUT"
echo "taille: $(du -h "$OUT" | cut -f1)"
echo
echo "Installation: copier srb2kart_ps3.pkg sur la PS3 (USB ou FTP), puis"
echo "  Jeu > Gestionnaire de paquets > Paquets d'installation."
echo "Il s'installe dans /dev_hdd0/game/$APPID/ et ecrit ses logs dans USRDIR/."
