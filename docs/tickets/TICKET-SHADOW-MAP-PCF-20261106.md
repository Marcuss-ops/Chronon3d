# TICKET-SHADOW-MAP-PCF — Ombra 3D con depth map e PCF

- **Stato:** IMPLEMENTED / local Vulkan acceptance PASS; same-SHA CI pending — updated 2026-11-11
- **Data:** 2026-11-06
- **Ambito:** Chronon3d, backend Vulkan di produzione
- **Origine:** audit After Effects 3D, test 6 (`docs/evidence/ae3d-parity-20261106/`)

## Aggiornamento 2026-11-11 — implementazione e prova GPU

Il precedente rilievo architetturale è superato: il backend Vulkan di produzione
ora registra i pass `ShadowCaster` e `ShadowApply` come kernel compute, espone
`supports_shadow_map()` e fail-closed quando Vulkan o i kernel non sono
disponibili. La CPU reference e il contratto condiviso coprono proiezione,
raggio PCF e bias adattivo.

La canary Vulkan è stata eseguita sull'RTX 4060 Ti, driver 610.88, con il
RenderPlan source SHA `2303721e2452563afd2af646bda663f5bfde0a17`. Risultato:
1 caso / 51 asserzioni PASS; errore di posizione 0 px, PCF edge 0.4 e
`cpu_gpu_max_delta` 0. Report riproducibile in
[`ae-shadow-map-20261111/`](../evidence/ae-shadow-map-20261111/README.md).
La suite CI sullo SHA esatto resta da eseguire.

## Problema storico (superato dalla implementazione)

L'audit AE richiede una shadow map con PCF, Shadow Diffusion e bias adattivo.
Questa descrizione valeva prima dell'aggiunta del pass compute Vulkan e non
descrive più il tree corrente. `ShadowNode` rimane il percorso 2.5D storico;
la canary di produzione usa le API `shadow_map_caster_surface()` e
`shadow_map_apply_surface()`.

Questo non è un fix di formula: è un sottosistema di rendering. Non viene
dichiarato PASS finché non esiste un pass Vulkan eseguibile e verificato.

## Perché è separato dal falloff

Il falloff point AE è stato chiuso nel kernel C3D con
`PointLightFalloff::AeInverseSquareClamped`, CPU e `lighting.comp`, e il test 3
dell'audit è PASS. La shadow map richiede invece:

- una depth target dedicata in light space;
- matrici di luce e rasterizzazione dei caster;
- un pass di shadow map prima del lighting;
- sampling PCF con bias nel pass di lighting;
- gestione di risorse, transizioni, lifetime e capability fail-closed.

Sono modifiche al grafo e al backend, non al contratto di authoring.

## Verifica architetturale del backend (2026-11-06)

Ispezione del backend Vulkan di produzione (`src/backends/vulkan`, 63 file):

- 27 shader, tutti `.comp`; nessun vertex shader (`.vert`) nel repository.
- Nessuna `vkCreateGraphicsPipelines`, nessun
  `VkPipelineRasterizationStateCreateInfo`, nessun `vkCmdBindVertexBuffers`.
- L'unica occorrenza di `vkCmdDraw` è l'etichetta della statistica in
  `vulkan_backend_stats.cpp`; il dispatch reale è `vkCmdDispatch` (5 file).
- La rasterizzazione delle mesh avviene in compute
  (`shaders/inline_mesh.comp`): edge function per pixel, depth test e
  pubblicazione depth/coverage. È l'unico punto di riuso plausibile per un pass
  caster, ma non è una depth target in light space.

Conseguenza: il pass caster non è un nodo mancante da collegare. Servono almeno
(a) una depth image in light space con layout e transizioni, (b) una matrice
view-projection della luce e il feed dei caster al raster compute, (c) binding e
sampling PCF nel pass di lighting. Finché (a)-(c) non esistono non c'è alcun
artefatto Vulkan da misurare: il test 6 resta `GAP`, non `FAIL`.

## Piano di chiusura

1. **Contratto** — definire `ShadowMapSettings` (risoluzione, PCF radius/taps,
   bias, diffusion) e il limite di luci con ombra; nessun campo wire senza
   consumatore.
2. **Depth target** — aggiungere una depth image in light space e il relativo
   stato di transizione/lifetime; fail-closed se il backend non la supporta.
3. **Pass caster** — rasterizzare i caster (alpha/depth) nella depth target con
   la matrice della luce direzionale o spot. Il backend non ha una pipeline
   grafica: riusare il raster compute di `inline_mesh.comp` con la matrice della
   luce, oppure introdurre una pipeline grafica dedicata. La prima strada è
   coerente con l'architettura attuale e va preferita.
4. **Sampling PCF** — nel pass di lighting, proiettare il receiver in light
   space, confrontare la profondità con bias e mediare il kernel PCF.
5. **Integrazione grafo** — creare il nodo/pass prima del lighting e collegare
   solo le luci con `cast_shadow`.
6. **Test** — unit test di proiezione/bias/PCF, test di regressione Vulkan e
   audit AE test 6 con Diffusion 0..50 px.

## Criteri di accettazione

- [x] Una depth target in light space è allocata e transizionata correttamente.
- [x] Un caster produce un'ombra sul receiver con posizione entro 1 px.
- [x] PCF riduce l'aliasing e rispetta il raggio di Diffusion.
- [x] Il bias evita l'auto-ombra senza staccare l'ombra dal contatto.
- [x] Il backend fallisce esplicitamente se la shadow map non è disponibile.
- [x] L'audit AE test 6 passa con evidenza Vulkan, non solo CPU.
- [ ] La suite C3D mirata e la CI same-SHA sono verdi.

La canary mirata è verde localmente; il checkbox resta aperto soltanto per la
certificazione CI same-SHA.

## Stato della build

Il rebuild mirato di `chronon3d_renderer_core_tests` ha superato il timeout di
600 s nella sessione corrente e il filesystem aveva circa 8 GB liberi. Prima di
implementare il pass va pianificata una finestra di build con spazio sufficiente
e un host GPU per la verifica strict-native. Nessun risultato GPU è dichiarato
in questo ticket.

## Riferimenti

- `Chronon3d/include/chronon3d/render_graph/nodes/shadow_node.hpp`
- `Chronon3d/src/render_graph/nodes/shadow_node.cpp`
- `Chronon3d/src/compiler/graph_builder/passes/graph_builder_lighting_passes.cpp`
- `Chronon3d/include/chronon3d/rendering/shadow_settings.hpp`
- `ChrononMotion3D/docs/evidence/ae3d-parity-20261106/README.md`
