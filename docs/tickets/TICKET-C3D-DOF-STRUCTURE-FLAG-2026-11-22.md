# TICKET-C3D-DOF-STRUCTURE-FLAG-2026-11-22 — DoF senza forzare il rebuild del grafo

**Stato:** `DONE`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** temporal pipeline / graph cache
**Origine:** C3D-AUDIT-04
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)

## Problema

`scene_temporal_execution.cpp` forza `graph_structure_unchanged = false` ogni
volta che la camera 2.5D ha DoF attivo. Il commento spiega il motivo: il
refresh della cached graph non aggiorna atomicamente lo stato di depth-tracking
del DoF, quindi un rebuild preserva la determinismo warm/cold.

Il costo è che un cambio **solo di parametri DoF** (focus, apertura, ecc.)
ricostruisce il grafo, anche se la topologia non è cambiata.

## Evidenza

- `[CODICE]` `src/render_graph/pipeline/temporal/scene_temporal_execution.cpp:199-208`:
  ```cpp
  if (ctx.frame_input.has_camera_2_5d &&
      ctx.frame_input.camera_2_5d.dof.enabled) {
      ctx.policy.graph_structure_unchanged = false;
  }
  ```
- `[COUNTER]` `graph_cache_refresh_fallback_total` esiste già
  (`include/chronon3d/core/profiling/render_counter_macros.hpp:292`) ed è
  incrementato in `src/render_graph/pipeline/graph_cache_coordinator.cpp:320-344`.
- `[TEST]` `tests/render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp`:
  - `:239` due render consecutivi byte-equal;
  - `:407` cache invalida quando la posizione animata cambia;
  - `:444` cache warm hit preserva output e coverage.
- `[TEST]` `chronon3d_deterministic_tests` 94/94 verde, ma non copre il caso
  “solo parametri DoF cambiati, topologia costante”.

## Piano di chiusura

1. **Mappare lo stato DoF.** Individuare nel grafo compilato i nodi che
   consumano `DepthOfFieldSettings`/depth buffer e il punto in cui il refresh
   cached dovrebbe aggiornarli (`graph_cache_coordinator.cpp`,
   `graph_compile_pipeline.cpp`, nodi `per_pixel_dof`).
2. **Refresh atomico.** Rendere i parametri DoF parte del payload per-frame
   aggiornato dal refresh, oppure invalidare selettivamente solo il sotto-grafo
   DoF. Non lasciare nodi con settings vecchi e depth nuovo.
3. **Mantenere il flag.** Con il refresh atomico, lasciare
   `graph_structure_unchanged=true` quando cambiano solo i parametri DoF.
4. **Fixture di determinismo.** Aggiungere un caso warm/cold:
   - render freddo frame N con DoF;
   - render caldo dello stesso frame con cache popolata;
   - stesso SHA byte-per-byte.
5. **Contatore.** Usare `graph_cache_refresh_fallback_total` e il diagnostico
   `build_fresh_candidate`/`refresh_cached_candidate`; il primo deve essere 0
   sui frame a topologia costante.
6. **Fallback sicuro.** Se il refresh atomico non è realizzabile, non rimuovere
   il flag: chiudere il ticket come “won't fix” solo con la prova che il
   rebuild è necessario e con il costo misurato.

## Gate di accettazione

- [ ] Frame con soli parametri DoF cambiati: `graph_structure_unchanged=true`.
- [ ] `graph_cache_refresh_fallback_total` delta = 0 su topologia costante.
- [ ] SHA warm/cold identico sul frame DoF.
- [ ] I test DoF esistenti restano verdi.
- [ ] Nessuna regressione su camera animata/topologia cambiata (cache miss
      atteso).

## Comandi di verifica

```bash
ninja -C build/chronon/linux-video-test -j2 chronon3d_render_graph_tests
./build/chronon/linux-video-test/tests/chronon3d_render_graph_tests \
  -tc='PR2-RG-DoF*'
./build/chronon/linux-video-test/tests/chronon3d_deterministic_tests
```

## Rischi e rollback

- Il rischio è un output warm diverso dal cold per depth/settings disallineati:
  il gate SHA è bloccante.
- Rollback immediato: ripristinare il forcing a `false`, che è il
  comportamento attuale certificato.

## Definition of Done

Il ticket si chiude quando il grafo non viene ricostruito per soli cambi DoF,
il contatore di fallback refresh resta a 0 e warm/cold hanno lo stesso SHA.
Fino ad allora il flag resta e il ticket resta aperto.

## Esito verifica — 2026-11-22

**Chiuso sul percorso CPU/software.**

- Il forcing DoF è rimosso; il refresh cached imposta `track_dof_depth`,
  resetta il buffer condiviso `dof_depth_buffer()` e aggiorna il payload
  `CompositeNode` (world position/z) dal layer corrente.
- Il validatore accetta gli effect intrinseci (`PerPixelDofNode`, lighting,
  shadow, depth-grade) senza confonderli con `EffectStackNode`.
- Test mirato `PR2-RG-DoF: cache warm hit...`: 9/9; SHA warm == cold,
  `graph_cache_hits > 0`, `graph_cache_refresh_fallback_total == 0`.
- Test z-animation / animated-position / ROI: 4/4, 5/5, 901/901.
- `chronon3d_render_graph_tests`: 470/470, 19945 assertions.
- `chronon3d_deterministic_tests`: 94/94, 11797 assertions.

Limite: la verifica è sul backend CPU/software; la lane GPU strict non è
oggetto di questo ticket.
