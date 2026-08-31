#!/bin/bash
# Relink the game without -lnet -lsysmodule.  NONET=1 means the game never opens
# a socket, yet the binary imports sys_net and cellSysmodule -- two modules lv2
# has to resolve at process load.  If the link succeeds, nothing needed them.
# If it fails, the error names exactly what does.
#
# Made portable for the SRB2Kart-PS3 repo on 2026-08-31 (was hardcoded to
# /mnt/d/_PS3_Projects/SRB2Kart -- see git history in the private project
# tree for that version).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -z "$PS3DEV" ] && [ -f "$HOME/PS3DK/scripts/env.sh" ]; then
	# shellcheck disable=SC1091
	source "$HOME/PS3DK/scripts/env.sh"
fi
PS3DEV="${PS3DEV:-$HOME/PS3DK/stage/ps3dev}"
export PS3DEV
export PS3DK="$PS3DEV/ps3dk"
export PATH="$PS3DEV/portlibs/ppu/bin:$PATH"

cd "$REPO_ROOT/src" || exit 1

# Force the link step to re-run; the objects themselves are unchanged.
rm -f "$REPO_ROOT/src/bin/Release/srb2kart_ps3.elf"

make SDL=1 PS3GCM=1 PREFIX=powerpc64-ps3-elf NONX86=1 NOASM=1 NOHW=1 NOMIXER=1 NONET=1 NOPNG=1 \
     SDL_CONFIG=sdl2-config \
     ZLIB_CFLAGS="-I$PS3DEV/portlibs/ppu/include" \
     ZLIB_LDFLAGS="-L$PS3DEV/portlibs/ppu/lib -lz -L$SCRIPT_DIR/ps3-compat -lps3compat" \
     CPPFLAGS="-D_PS3 -fno-jump-tables" EXENAME=srb2kart_ps3.elf ECHO=1 -j"$(nproc)" 2>&1 | tail -40
echo "MAKE_EXIT=${PIPESTATUS[0]}"
