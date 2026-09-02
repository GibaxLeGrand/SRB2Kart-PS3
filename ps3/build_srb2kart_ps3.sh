#!/bin/bash
# Build srb2kart_ps3.elf against a PS3DK toolchain.  2026-08-31, made portable
# for the SRB2Kart-PS3 repo (was originally a machine-local script hardcoded
# to /mnt/d/_PS3_Projects/SRB2Kart -- see git history in the private project
# tree for that version).
#
# Local dev: source PS3DK's scripts/env.sh yourself first, or let this script
# find it at ~/PS3DK if that is where you keep the toolchain checkout.
# CI: exports PS3DEV itself (extracted from the prebuilt toolchain artifact,
# see .github/workflows/ps3-ci.yml) and this script skips the env.sh lookup.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ -z "$PS3DEV" ] && [ -f "$HOME/PS3DK/scripts/env.sh" ]; then
	# shellcheck disable=SC1091
	source "$HOME/PS3DK/scripts/env.sh"
fi
: "${PS3DEV:?PS3DEV not set -- source PS3DK scripts/env.sh, or export PS3DEV yourself}"

export PATH="$PS3DEV/portlibs/ppu/bin:$PATH"
# The PPU GCC driver reads this via getenv() at link time (LIB_LV2_SPEC,
# baked into the cross-compiler's own specs) to find libsysbase/liblv2/etc.
# Missing it is a link-time "fatal error: environment variable 'PS3DK' not
# defined", not a compile error, so it slips past every .c file first.
export PS3DK="$PS3DEV/ps3dk"
PREFIX=powerpc64-ps3-elf

# ps3-compat: PSL1GHT-style sysUtil callback forwarders that PS3DK's own
# compat layer does not provide (see ps3-compat/ps3_sysutil_compat.c for
# why). Not committed as a prebuilt .a -- PPU object code is ABI-tied to the
# exact toolchain build, so it is rebuilt from source every time instead.
echo "=== building ps3-compat ==="
command -v "${PREFIX}-gcc" >/dev/null || { echo "MANQUE: ${PREFIX}-gcc (PATH n'a pas le toolchain PPU)"; exit 1; }
"${PREFIX}-gcc" -c "$SCRIPT_DIR/ps3-compat/ps3_sysutil_compat.c" -o "$SCRIPT_DIR/ps3-compat/ps3_sysutil_compat.o"
"${PREFIX}-ar" rcs "$SCRIPT_DIR/ps3-compat/libps3compat.a" "$SCRIPT_DIR/ps3-compat/ps3_sysutil_compat.o"

# 2026-09-02 -- renderer : logiciel par defaut, OpenGL sur demande.
#
# Rappel de vocabulaire, parce que le nom trompe : dans SRB2, HWRENDER *est*
# le renderer OpenGL ("HW" = hardware, par opposition au logiciel). NOHW=1
# construit donc le chemin LOGICIEL, et retirer NOHW active OpenGL.
#
#   GLRENDER=1 ./build_srb2kart_ps3.sh   -> OpenGL, via PSGL
#   (rien)                               -> logiciel, comme jusqu'ici
#
# Le chemin OpenGL est neuf (etape A5 de AUDIT_VITA_20260902.md) : il compile
# et lie, mais n'a pas encore affiche une image. Le logiciel reste le defaut
# tant que ce n'est pas verifie.
if [ -n "$GLRENDER" ]; then
	HW_FLAGS="PS3DK_GLCOMPAT=$SCRIPT_DIR/glcompat"
	echo "=== renderer : OpenGL (PSGL) ==="
else
	HW_FLAGS="NOHW=1"
	echo "=== renderer : logiciel ==="
fi

echo "=== building srb2kart_ps3.elf ==="
cd "$REPO_ROOT/src"
make SDL=1 PS3GCM=1 PREFIX="$PREFIX" NONX86=1 NOASM=1 $HW_FLAGS NOMIXER=1 NONET=1 NOPNG=1 \
     SDL_CONFIG=sdl2-config \
     ZLIB_CFLAGS="-I$PS3DEV/portlibs/ppu/include" \
     ZLIB_LDFLAGS="-L$PS3DEV/portlibs/ppu/lib -lz -L$SCRIPT_DIR/ps3-compat -lps3compat" \
     CPPFLAGS="-D_PS3 -fno-jump-tables" EXENAME=srb2kart_ps3.elf ECHO=1 -j"$(nproc)"
echo "MAKE_EXIT=$?"
