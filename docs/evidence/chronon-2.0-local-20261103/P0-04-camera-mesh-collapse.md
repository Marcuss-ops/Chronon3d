# P0-04 — camera rotation + `inline_mesh.animation` collapse (2026-11-03)

**Stato: fix locale verificato.** Il difetto era il clip far-plane, non la
proiezione della camera e non il campionamento della mesh.

## Sintomo riprodotto

Fixture: `ChrononMotion3D/docs/evidence/showcase-animation-20261103/plans/03-gltf-animato.plan.json`
(1056×588, mesh inline animata, fondale `plane3d`). Con `camera_samples` di sola
posizione il triangolo resta ~20 250 px per 60 frame; basta una rotazione Y
totale di **0,02°** perché la copertura crolli:

| frame | solo posizione | rotazione 0,02° |
|---|---|---|
| 0 | 20 248 px | 20 248 px |
| 10 | — | 7 902 px |
| 20 | 20 246 px | 5 623 px |
| 40 | 20 248 px | 276 px |
| 59 | 20 249 px | 0 px |

Il bbox dei vertici deformati e le matrici di modello sono identici nei due
casi: la mesh viene disegnata, ma **clippata**.

## Causa

`SoftwareMeshProcessor::draw()` (e il gemello `try_native_inline_mesh`) proietta
con il `ProjectionContext` 2.5D ma clippa con `ctx.frame_input.camera.near_plane`
/ `far_plane`. La `Camera` nativa è costruita con i default
`near = 0.1`, `far = 1000.0`, mentre la scena 2.5D vive **esattamente a 1000
unità** dalla camera (`camera.position.z = -1000`, `zoom/focal = 1000`). Con la
rotazione a zero i vertici stanno sul piano far (z = −1000) e il predicato
`v.z + far_plane >= 0` li tiene; con una rotazione anche minima alcuni vertici
finiscono a z < −1000 e il far plane taglia il triangolo, lasciando solo la
fettina che resta dentro il frustum. Il valore `far = 1000` non è quello
autorevole: il piano RenderPlan dichiara `near = 1`, `far = 10000`.

## Fix

Una sola autorità per il clip range:

1. `Camera2_5D` porta `near_plane` / `far_plane` (default 1 / 10000), letti dal
   `CameraPlan` in `make_camera_value()`.
2. `scene_context_setup.cpp`, `debug.cpp`, `preflight.cpp` copiano il range
   autorevole sulla `Camera` nativa del frame input.
3. I processor mesh continuano a leggere `camera.near_plane/far_plane`: ora è
   lo stesso range della proiezione 2.5D con cui disegnano.

Nessun cambio di contratto: il piano che non dichiara `near`/`far` eredita i
default del decoder (1 / 10000), già usati dalla `CameraPlan`.

## Verifica

Comando (dalla root del workspace, build `linux-video-release`):

```bash
chronon3d_cli render --plan /tmp/repro/rot.plan.json \
  --assets-root ChrononMotion3D/docs/evidence/showcase-animation-20261103 \
  --backend software --start-frame 0 --end-frame 59 \
  --video-sink raw --output /tmp/repro/rot-fixed.rgba -v error
```

Esito dopo il fix (build Release `linux-video-release`, `BUILD_EXIT=0`, 269
oggetti, ccache + mold): **copertura ≥ 20 100 px su tutti i 60 frame**.

| frame | pre-fix (rotazione) | post-fix |
|---|---|---|
| 0 | 20 248 px | 20 248 px |
| 10 | 7 902 px | 20 249 px |
| 20 | 5 623 px | 20 235 px |
| 40 | 276 px | 20 247 px |
| 59 | 0 px | 20 249 px |

Minimo 20 100 px (frame 35), massimo 20 254 px: la variazione residua è il
cambio di prospettiva della rotazione, non un clip. Il criterio “stessa
tolleranza del caso a sola posizione” è soddisfatto.

## Nota di scope

Questo chiude il collasso camera+mesh animata sul percorso software. Il render
Vulkan strict-native della stessa fixture resta un gate separato (P0-13), e la
review visiva umana resta `BLOCKED` (P1-16).
