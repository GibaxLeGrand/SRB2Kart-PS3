# Travail sur libPSGL — mis de côté le 06/09/2026

Le renderer OpenGL du portage PS3 était bâti sur **PSGL**, la réimplémentation
d'OpenGL ES 1.1 fournie par PS3DK. Ce chemin est mis de côté au profit d'une
couche maison sur RSX brut, à la manière d'IoQuake3-PS3.

Ce dossier existe parce que **le dépôt PS3DK n'a aucun dépôt distant** : sans
ça, tout ce travail ne vivrait que sur une seule machine.

## Ce qu'il y a ici

| Fichier | Contenu |
|---|---|
| `0001-libPSGL-implementation-de-glTexEnv-*.patch` | `glTexEnv*` implémenté : 7 shaders Cg, clés texenv à la manière d'IoQuake3, deux unités de texture |
| `0002-libPSGL-borner-l-attente-d-etiquette-*.patch` | Attente d'étiquette bornée dans `psgl_device_create` + crochet de trace `psgl_trace_hook` (marqueurs D1..D10) |
| `libPSGL-sources-20260906.tar.gz` | `sdk/libPSGL` complet à cet instant, sauvegardes et objets exclus |

Base : commit `666c3ea` de PS3DK (« local checkout, from release zip, no
upstream git history »).

## Pour réappliquer

```bash
cd ~/PS3DK
git am ps3/psgl-patches/0001-*.patch ps3/psgl-patches/0002-*.patch
```

Puis reconstruire — attention, **un seul jeu d'en-têtes enveloppes sur le chemin
d'inclusion**, sinon leur `#include_next` retombe sur l'autre copie et leur
garde-fou se déclenche :

```bash
cd ~/PS3DK/sdk/libPSGL
make CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -mcpu=cell -std=c11 -I$PS3DK/ppu/include"
make CFLAGS="..." install
```

## Ce qui marchait, et ce qui ne marchait pas

**Acquis, mesuré sur la PS3 réelle le 05-06/09** — le binaire OpenGL démarre,
`psgl_device_create` va jusqu'au bout (marqueurs D1..D10), le périphérique est
créé en 1920×1080 avec un pitch cohérent de 7680, et toute l'initialisation
OpenGL passe (`C1a`..`C10`). Sous RPCS3, le même binaire atteint l'écran-titre
à **60 fps** et 420 swaps.

**Non résolu** :

- **Mort du FIFO RSX** vers 2:00 sous RPCS3 (`Dead FIFO commands queue state`).
  Jamais observée sur matériel, faute d'y arriver.
- **`glCopyTexImage2D` ne copie que la profondeur** → la passe d'écran final est
  contournée, les wipes sont désactivés. Même symptôme que le blocage n°4 du
  portage Vita.
- **La destruction de texture plante** → contournée par une fuite volontaire
  dans `Flush()`.
- Sept entrées GL manquantes, toutes des variantes de nom ou de type
  (`glClearDepth`, `glDepthRange`, `glMateriali`, `glMultMatrixd`,
  `glColor4ubv`, `glMultiTexCoord2f`, `glMultiTexCoord2fv`).

**Important** : le blocage qui a motivé l'abandon — `R_AddSkins` sur
`chars.kart` — **n'est pas imputable à PSGL**. Aucun appel n'y entre dans la
bibliothèque : `HWR_AddPlayerMD2` se contente d'analyser `mdls.dat`, et
`ST_LoadFaceGraphics` s'arrête au cache de patches du moteur. Il faudra le
résoudre quelle que soit la couche graphique.

Contexte complet : `AUDIT_PS3_20260905.md` et `PLAN_20260906.md` à la racine du
projet, branche `psgl-renderer-20260906` pour l'état du code côté jeu.
