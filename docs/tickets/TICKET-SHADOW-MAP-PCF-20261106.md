# TICKET-SHADOW-MAP-PCF — Ombra 3D con depth map e PCF

- **Stato:** OPEN / architecture-gated — 2026-11-06
- **Data:** 2026-11-06
- **Ambito:** Chronon3d, backend Vulkan di produzione
- **Origine:** audit After Effects 3D, test 6 (`docs/evidence/ae3d-parity-20261106/`)

## Problema

L'audit AE richiede una shadow map con PCF, Shadow Diffusion e bias adattivo.
Il backend Vulkan di produzione non ha né una depth target in light space né un
pass di sampling delle ombre. `ShadowNode` proietta l'alpha del caster in screen
space e applica un blur separabile: è un'ombra 2.5D, non una shadow map di
scena. Il PCF OpenGL in `render/` è look-dev e non è evidenza per Vulkan.

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

- [ ] Una depth target in light space è allocata e transizionata correttamente.
- [ ] Un caster produce un'ombra sul receiver con posizione entro 1 px.
- [ ] PCF riduce l'aliasing e rispetta il raggio di Diffusion.
- [ ] Il bias evita l'auto-ombra senza staccare l'ombra dal contatto.
- [ ] Il backend fallisce esplicitamente se la shadow map non è disponibile.
- [ ] L'audit AE test 6 passa con evidenza Vulkan, non solo CPU.
- [ ] La suite C3D mirata e la CI same-SHA sono verdi.

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
