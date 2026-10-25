# LEANUP-001: piano operativo per i ticket aperti

Aggiornato il 2026-10-25 da GitHub. Restano aperti 8 ticket in Chronon3D e 5 in ChrononMotion3D. I ticket #56 e #59 risultano chiusi.

## Sequenza consigliata

1. **Sbloccare la catena Chronon3D già aperta**: verificare i fallimenti CI della PR [#61](https://github.com/Marcuss-ops/Chronon3d/pull/61), correggerli dove sono introdotti dalla PR e completare i controlli. Poi integrare in ordine #61 → [#62](https://github.com/Marcuss-ops/Chronon3d/pull/62) → [#63](https://github.com/Marcuss-ops/Chronon3d/pull/63), aggiornando la base di ogni PR dopo il merge precedente. Le #62 e #63 sono impilate su queste basi e non vanno integrate fuori ordine.
2. **Completare Wave 2 Vulkan**: affrontare #51 e #52 coordinando i cambiamenti con il lavoro #53 già in PR, senza duplicare lo split del backend. Fare prima l'inventario delle unità e dei test di parità, poi spostare un dominio per volta.
3. **Finire render plan e test Chronon3D**: con i cambiamenti dei pass e degli header stabili, dividere gli header del ticket #57 e poi i test monolitici di #58, preservando il conteggio delle asserzioni.
4. **Fare il ticket indipendente #60**: completare l'inventario dei commenti storici, rimuovendo solo narrazione su codice eliminato e mantenendo commenti che descrivono contratti o comportamento attuale.
5. **ChrononMotion3D**: #5 è indipendente; per gli altri, completare #4 prima di consolidare i walk JSON in #3. Fare #1 prima di #2, che esplicitamente dipende dalla valutazione dei descrittori dopo #1.

## Ticket e risultato richiesto

| Repo / issue | Intervento concreto | Gate di chiusura |
|---|---|---|
| [Chronon3D #51](https://github.com/Marcuss-ops/Chronon3d/issues/51) | Spezzare composite, text, surface e lifecycle in unità sotto 500 righe; dividere `record_composite` per geometry, material, descriptors e submit; rendere obbligatori i test registry↔Vulkan. | Parità Vulkan, replay timestamp e test surface; nessuna asserzione rimossa. |
| [Chronon3D #52](https://github.com/Marcuss-ops/Chronon3d/issues/52) | Estrarre `vulkan_backend_impl.hpp` in implementazione `.cpp`/detail mantenendo gli `static_assert` ABI nel punto in cui sono definiti i layout. | Build e test Vulkan della Wave 2, inclusa parità registry↔Vulkan. |
| [Chronon3D #53](https://github.com/Marcuss-ops/Chronon3d/issues/53) | Spostare la logica inline di surface retirement da header e unità private verso `.cpp`. PR #61 esistente. | Parità Vulkan, replay timestamp, test surface. |
| [Chronon3D #54](https://github.com/Marcuss-ops/Chronon3d/issues/54) | Spostare i dettagli inline di `render_backend.hpp`, `render_graph_hashing.hpp`, `render_graph_context.hpp` e `node_memory_tracker.hpp`, lasciando i contratti negli header. PR #62 esistente. | Build completa e test hash/contatori. |
| [Chronon3D #55](https://github.com/Marcuss-ops/Chronon3d/issues/55) | Dividere `composition.cpp` per pass e `scene_temporal_execution.cpp` in preparazione, esecuzione e verifica. PR #63 esistente. | Test pipeline. |
| [Chronon3D #57](https://github.com/Marcuss-ops/Chronon3d/issues/57) | Separare `LayerPlan`, `EffectPlan`, `TextPlan` e `GradientPlan` in header gemelli; mantenere `render_plan.hpp` aggregatore. | Build completa. |
| [Chronon3D #58](https://github.com/Marcuss-ops/Chronon3d/issues/58) | Estrarre fixture/helper condivisi e dividere per topic shape, text ed effects i quattro test indicati nel ticket; puntare a file sotto 1.000 righe. | Conteggio totale delle asserzioni invariato. |
| [Chronon3D #60](https://github.com/Marcuss-ops/Chronon3d/issues/60) | Completare l'inventario e togliere i commenti che narrano codice già rimosso. | Verifiche statiche e test pertinenti; contratti attuali conservati. |
| [ChrononMotion3D #1](https://github.com/Marcuss-ops/ChrononMotion3d/issues/1) | Scomporre `RenderPlanEmissionValidation` per dominio. | Test emission/validation. |
| [ChrononMotion3D #2](https://github.com/Marcuss-ops/ChrononMotion3d/issues/2) | Dividere `RenderPlanEmission.cpp` per layer e `RenderPlanLowering.cpp` per operazione, inclusi morph e `applyBakedPathMorph`; valutare descrittori condivisi con validation. | Test round-trip emission/lowering. |
| [ChrononMotion3D #3](https://github.com/Marcuss-ops/ChrononMotion3d/issues/3) | Separare `SceneExport.cpp` e `SceneImport.cpp` per layers, lights, animation e metadata; ridurre walk JSON duplicati. | Test round-trip import/export. |
| [ChrononMotion3D #4](https://github.com/Marcuss-ops/ChrononMotion3d/issues/4) | Completare lo split di `GltfReader.cpp` in scenegraph e animazioni e valutare il riuso dei walk JSON con SceneImport. | Test import. |
| [ChrononMotion3D #5](https://github.com/Marcuss-ops/ChrononMotion3d/issues/5) | Estrarre fixture condivise, dividere i test authoring/showcase/geospatial/presets e separare `export_camera_trajectories.cpp` per sottocomando. | Conteggio dei test invariato. |

## Build e verifiche con il minor lavoro ripetuto

- Prima di ogni refactor, identificare i target e i test esistenti con `cmake --build <build-dir> --target help` e `ctest --test-dir <build-dir> -N`; non indovinare nomi di target.
- Riutilizzare un build tree Linux Release per repo, con CCache già disponibile; compilare solo il target interessato con `cmake --build <build-dir> --target <target> -j16` e lanciare la suite mirata con `ctest --test-dir <build-dir> -R '<suite>' --output-on-failure`.
- Registrare prima/dopo il numero di test e asserzioni per #58 e ChrononMotion #5. Per le modifiche Vulkan eseguire tutte le suite parità/replay/surface richieste insieme, dopo la relativa tranche.
- Eseguire la matrice CI completa una volta per ogni PR/wave integrabile. Al controllo del 2026-10-25 le PR #61–#63 risultavano `UNSTABLE`; molti controlli della #61 erano rossi, quindi prima di aggiungere altre modifiche occorre distinguere regressioni del codice da fallimenti già presenti nel gate.
- Per ogni chiusura annotare commit/PR, comando eseguito e risultato del gate; per le modifiche visive aggiungere un frame/render di prova.

## Limiti del piano

Il piano ordina il lavoro e indica i gate dichiarati nei ticket; non presume che ogni ticket sia chiudibile senza risolvere i controlli CI instabili o eventuali regressioni. L'ordine dei cambiamenti #51–#55 va confermato contro i diff delle PR e i conflitti reali prima di intervenire.
