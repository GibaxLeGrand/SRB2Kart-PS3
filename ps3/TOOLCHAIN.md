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
