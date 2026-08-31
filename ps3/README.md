# SRB2Kart — PS3 port

A homebrew port of [SRB2Kart](https://kartkrew.org/) to the PlayStation 3,
built against [PS3DK](https://git.rpcs3.net/rpcs3/PS3DK) (the RPCS3 team's
modern PPU/SPU toolchain — GCC 12.4.0 PPU, C++17-20, SDL2 as a portlib).
Not associated with Sony Interactive Entertainment or the SRB2Kart/Kart
Krew team; do not contact either for support with this fork.

Status as of 2026-08-31 — the game boots and races on real hardware, with
sound, but has one open regression:

| Subsystem | State |
|---|---|
| Boots on real PS3 | ✅ |
| Controller | ✅ recognised, bound, playable |
| Display | ✅ 320×200, RSX-scaled |
| Memory (`PU_CACHE`) | ✅ 138MB → 2.9MB |
| Sound & music | ✅ Ogg Vorbis via SDL2_mixer, 48kHz assets |
| Framerate | 🟡 ~35fps average |
| Player sprite | 🔴 **freezes mid-race** — regression introduced alongside the sound build, root cause not yet found (`mobj->tics` stops decrementing; the `animonly` label in `P_PlayerMobjThinker` is never reached for the player's own mobj — see instrumentation notes in the private project tree) |
| Network | ❌ compiled `NONET=1` |
| Hardware (OpenGL/RSX) rendering | ❌ not started — software renderer only |

The CI-buildable configuration here (`ps3/build_srb2kart_ps3.sh`) is the
**silent (`NOMIXER=1`)** variant, which does not have the sprite-freeze
regression — it's the known-good, reproducible build. The sound-enabled
build that does have the bug was assembled by hand during debugging and
isn't wired into a committed script yet.

## Layout

- `ps3/build_srb2kart_ps3.sh` — compiles `srb2kart_ps3.elf` (software
  renderer, no mixer, no network, no PNG) against a PS3DK toolchain.
- `ps3/package_srb2kart_pkg.sh` — packages an XMB-installable `.pkg`
  (`LIGHT=1` for an eboot-only package that doesn't carry game data — see
  below).
- `ps3/relink_nonet.sh` — forces a relink under `NONET=1` to confirm the
  binary doesn't actually need `sys_net`/`cellSysmodule` at load time.
- `ps3/ps3-compat/` — a handful of PSL1GHT-style `sysUtil*` callback
  forwarders PS3DK's own compat layer doesn't provide; SDL's PS3 event
  backend calls the old lowercase names. Rebuilt from source by the build
  script, not committed as a prebuilt `.a`.
- `ps3/sfo.xml`, `ps3/ICON0.PNG` — package metadata and XMB icon.
- `ps3/TOOLCHAIN.md` — how CI gets a PS3DK toolchain without building one
  from source on every push.
- `.github/workflows/ps3-ci.yml` — builds the ELF and a LIGHT `.pkg` on
  every push.

## Game data is not in this repo

`srb2.srb`, `gfx.kart`, `textures.kart`, `chars.kart`, `maps.kart`,
`music.kart`, `sounds.kart`, `bonuschars.kart` (~500MB combined) are
SRB2Kart's own game assets, distributed separately by the Kart Krew — not
vendored here, and not something CI can produce. That's why
`package_srb2kart_pkg.sh` defaults to `LIGHT=1` in CI: an eboot-only `.pkg`
that installs onto a console that already has the full game data in place
from an official install. Building a full package locally (outside CI)
still works if you point `BIN=` at a directory that has the data files
alongside the compiled `.elf`.

## Building locally

Needs a working PS3DK checkout (see PS3DK's own README for the WSL2 +
bootstrap process — it's a from-source build, expect it to take a while
the first time).

```bash
source /path/to/PS3DK/scripts/env.sh   # exports PS3DEV and puts the
                                        # cross-compilers on PATH
bash ps3/build_srb2kart_ps3.sh
LIGHT=1 bash ps3/package_srb2kart_pkg.sh
```

The build script also looks for a toolchain at `~/PS3DK` on its own if
`PS3DEV` isn't already set, so sourcing `env.sh` yourself is optional if
that's where your checkout lives.
