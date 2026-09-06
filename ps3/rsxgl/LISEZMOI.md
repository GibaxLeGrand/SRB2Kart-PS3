# `rsxgl` — couche OpenGL sur RSX, reprise d'IoQuake3-PS3

**06/09/2026.** Fondation qui remplace PSGL. Le code vient de
[`IoQuake3-PS3`](https://github.com/) (`code/gl/`), un portage qui **tourne sur
console réelle** — ce n'est donc pas une réécriture depuis zéro.

## Pourquoi

PSGL (la réimplémentation d'OpenGL ES fournie par PS3DK) laissait trois
inconnues qu'on ne maîtrisait pas : mort du FIFO RSX, `glCopyTexImage2D` qui ne
copie que la profondeur, destruction de texture qui plante. Ici tout le chemin
RSX est à nous, donc débogable.

Le travail PSGL n'est pas perdu : branche `psgl-renderer-20260906` et
`ps3/psgl-patches/`.

## Périmètre, mesuré le 06/09

| | |
|---|---|
| Entrées `gl*` utilisées par `hardware/r_opengl/r_opengl.c` | 81 |
| dont derrière `#ifdef GL_SHADERS` — **non défini dans le build PS3** | −20 |
| **À fournir** | **61** |
| **Déjà fournies par cette couche** (suffixes ARB/EXT normalisés) | **47** |
| **À écrire** | **14** |

Sur les 14 : 4 no-ops (VBO de modèles), 3 alias de type, 2 brouillard,
4 éclairage de modèles (hors périmètre pour l'instant), et **une seule vraie
difficulté : `glCopyTexImage2D`**.

`glTexEnvi` — tout ce qu'on avait dû ajouter à PSGL — est déjà là.

## Contenu

| Fichier | Rôle |
|---|---|
| `ps3gl.h` | types, état global, prototypes |
| `ps3gl_main.c` | init, arrêt, cadre de frame |
| `ps3gl_states.c` | états de rendu (mélange, profondeur, ciseau…) |
| `ps3gl_textures.c` | textures, mipmaps |
| `ps3gl_matrices.c` | piles de matrices |
| `ps3gl_vertices.c` | anneau de sommets en mémoire RSX |
| `ps3gl_draw.c` | `DrawArrays` / `DrawElements` |
| `ps3gl_shaders.c` | sélection des programmes Cg par clé texenv |
| `ps3gl_colors.c` | conversion de couleurs |
| `ps3gl_shader_data.h` | microcode des shaders, précompilé |
| `GL/gl.h` | en-tête GL d'IoQuake3, pour **cette couche uniquement** |
| **`rsxgl_glapi.c`** | **passerelle `gl*` → `ps3gl_*`** — les 47 enveloppes sont générées depuis les prototypes réels, pas recopiées |

Le seul symbole que la couche attend de son hôte, `ps3_log()`, est fourni
par `src/sdl/ogl_rsx.c` — inutile d'avoir un fichier de colle séparé.

Le chemin SPU (`ps3gl_spu.c`) n'est **pas** repris : aucun autre fichier de la
couche ne le référence.

## État

**Tout compile contre PS3DK**, sans une seule erreur, dès la première tentative
pour les 8 fichiers d'origine.

**Il manque l'équivalent de leur `ps3_glimp.c`** (387 lignes) : création du
contexte RSX, framebuffers, flip. C'est ce qui permettra au jeu de lier et
d'afficher. C'est l'étape suivante.

## Construire

```bash
GLRENDER=1 RSXGL=1 ./ps3/build_srb2kart_ps3.sh
```

⚠️ Changer de renderer impose de purger `src/objs/SDL/Release/*.o`.

## Attention

`GL/gl.h` de ce dossier est **celui d'IoQuake3**, et il ne doit servir qu'aux
fichiers de cette couche. `r_opengl.c` continue d'utiliser
`ps3/glcompat/GL/gl.h`. Deux unités de traduction, deux chemins d'inclusion —
la passerelle fait le pont, et c'est justement son rôle.

Plan complet : [`PLAN_RSX_20260906.md`](../../../PLAN_RSX_20260906.md).
