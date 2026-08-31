# The CI toolchain tarball

`.github/workflows/ps3-ci.yml` compiles against a prebuilt PS3DK toolchain
downloaded from a GitHub Release on this repo (tag `toolchain-v1`, asset
`ps3dk-toolchain-linux-x86_64.tar.xz`), not built from source in CI.

## Why it works this way

PS3DK's only public repository is `git.rpcs3.net/rpcs3/PS3DK`, which blocks
automated/bot HTTP clients — that includes a GitHub Actions runner cloning
it, the same wall this project hit trying to fetch it for local dev. Even
without that, `bootstrap.sh` + `build-ppu-toolchain.sh` +
`build-spu-toolchain.sh` + `build-psl1ght.sh` + `build-portlibs.sh` is a
multi-hour job on its own — not something to redo on every push.

So the toolchain is built once, on a dev machine that already has it (WSL2,
see the main README), tarred up, and published as a release asset here. CI
downloads and caches that instead of building it.

## Rebuilding and republishing it

Needed when the pinned PS3DK version changes (new GCC, new patches, etc.) —
not for routine SRB2Kart-side changes.

```bash
# On the dev machine, inside the PS3DK checkout, after a normal build
# (bootstrap.sh, build-ppu-toolchain.sh, build-spu-toolchain.sh,
# build-psl1ght.sh, build-portlibs.sh have all been run):
cd "$PS3_TOOLCHAIN_ROOT/stage"
tar -I 'xz -T0 -6' -cf ps3dk-toolchain-linux-x86_64.tar.xz ps3dev

# Publish as a new release asset. Bump the tag (toolchain-v2, ...) rather
# than overwriting toolchain-v1 in place, then update TOOLCHAIN_TAG in
# .github/workflows/ps3-ci.yml to match.
gh release create toolchain-v2 ps3dk-toolchain-linux-x86_64.tar.xz \
    --repo GibaxLeGrand/SRB2Kart-PS3 \
    --title "PS3DK toolchain v2 (CI use only)" \
    --notes "Prebuilt ps3dev/ tree for ps3-ci.yml. Not a game release."
```

The tarball is `stage/ps3dev` only — the `$PS3DEV` install prefix (PPU/SPU
cross-compilers, PSL1GHT, portlibs, host tools like `make_self_npdrm` /
`sfo` / `pkg` / `sprxlinker`). Not the PS3DK source tree, not build
directories.

## The path has to match: `/home/comodore/PS3DK/stage/ps3dev`

Portlibs (SDL2, SDL2_mixer, libvorbis...) are built as ordinary autotools
packages with `--prefix=$PS3DEV`, and that prefix ends up hardcoded —
verbatim — into their `-config` scripts (`sdl2-config`), pkg-config `.pc`
files, libtool `.la` files, and CMake config files. They are **not
relocatable**. Extract the tarball anywhere other than the exact path it
was built at and those files still point at the old location.

The first CI run extracted to `$HOME` (`/home/runner/PS3DK/...` on a
GitHub-hosted runner) and failed on `SDL.h: No such file or directory` —
`sdl2-config --cflags` was still emitting
`-I/home/comodore/PS3DK/stage/ps3dev/portlibs/ppu/include/SDL2`, because
that is where the toolchain was built (a WSL2 Ubuntu-22.04 box, user
`comodore`). The header was sitting right there under the runner's own
`$HOME`; the stale path just never found it.

Fix: CI creates `/home/comodore` itself (`sudo mkdir` + `chown`, since
`/home` on the runner is root-owned and that user doesn't otherwise exist)
and extracts there, matching the build path exactly instead of patching
every config file that embeds it. **If you ever rebuild the toolchain on a
machine where the PS3DK checkout lives somewhere other than
`/home/comodore/PS3DK`, either rebuild it under that exact path first, or
update every hardcoded reference before tarring — and update the `PS3DEV`
env var in `.github/workflows/ps3-ci.yml` to match.**
