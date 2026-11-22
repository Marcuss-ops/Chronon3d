# TICKET-C3D-TOPOLOGY-ANIMATED-PATHS-2026-11-22 — base statica + path animati senza rimaterializzare la scena

**Stato:** `OPEN — implementazione e test CPU completati; baseline job SHA non misurato`
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** compiler / runtime pipeline
**Origine:** C3D-AUDIT-03
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)

## Problema

`RenderPipeline` bypassa la `topology_cache` quando il programma contiene path
tempo-variabili:

```cpp
const bool time_varying_paths =
    compiler::program_has_time_varying_paths(program);
if (time_varying_paths) {
    topology = materialize(static_cast<double>(frame.integral())).first;
}
```

Il commento è corretto: cachare il frame 0 congelerebbe l'animazione. Ma oggi
un solo path animato forza la materializzazione **dell'intera scena** a ogni
frame, anche se il 99% dei layer è statico.

## Evidenza

- `[CODICE]` `src/runtime/render_pipeline.cpp:197-208`.
- `[CODICE]` `src/compiler/scene_materializer.cpp:127`,
  `program_has_time_varying_paths` e `layer_has_time_varying_path`.
- `[EVIDENZA]` `docs/evidence/scene-topology-cache-20261103/README.md`:
  baseline con cache `scene_materialize_count` 72 → 9 e 1.496 → 5; il caso
  path animati resta fuori da quella cache.
- `[COUNTER]` `scene_materialize_count` e
  `scene_materialize_cache_hit_count` esistono già
  (`include/chronon3d/core/profiling/render_counter_macros.hpp:284-285`).

## Piano di chiusura

1. **Separare la base.** Introdurre una materializzazione “base statica” a
   frame 0 per i layer senza path tempo-variabili, riusando la stessa
   `topology_cache` e la stessa chiave `program.fingerprint_data()`.
2. **Solo path per-frame.** Materializzare a ogni frame esclusivamente i nodi
   con `shape_has_time_varying_path`; applicare il risultato come overlay/
   patch alla base. Non clonare l'intera scena se il contratto dei nodi consente
   un aggiornamento puntuale.
3. **Camera e luci.** Conservare il comportamento attuale: camera animata e
   luci animate continuano ad avere la copia per-render già prevista.
4. **Invalidazione.** La base si invalida su cambio di fingerprint/topologia
   reale; i path si aggiornano sul frame. Nessun hit su topologia cambiata.
5. **Test.** Aggiungere a `chronon3d_render_graph_tests` un caso con un solo
   path animato su 30 frame e un secondo caso con topologia che cambia.
6. **Telemetria.** Usare `scene_materialize_count` e
   `scene_materialize_cache_hit_count`; aggiungere un contatore
   `scene_path_update_count` se serve a distinguere base e path.

## Gate di accettazione

- [ ] Su 30 frame con un solo path animato, `scene_materialize_count` scende
      rispetto al baseline e la base viene materializzata una volta.
- [ ] SHA dei frame identico al baseline (nessun freeze, nessun path perso).
- [ ] Un cambio reale di topologia produce cache miss, non un hit stantio.
- [ ] `chronon3d_deterministic_tests` verde.
- [ ] Test di regressione per i path SVG/motion già citati nel commento del
      codice.

## Comandi di verifica

```bash
ninja -C build/chronon/linux-video-test -j2 chronon3d_render_graph_tests
./build/chronon/linux-video-test/tests/chronon3d_render_graph_tests \
  -tc='*topology*' -tc='*path*'
./build/chronon/linux-video-test/tests/chronon3d_deterministic_tests

# Sul job che produce il timing sidecar:
# prepare.scene_materialize_count e prepare.scene_materialize_cache_hits
```

## Rischi e rollback

- Il rischio principale è congelare l'animazione o perdere il paint del path.
  Il gate SHA è obbligatorio, non opzionale.
- Se l'overlay non è sicuro, il rollback è il branch attuale
  (`time_varying_paths → materialize(frame)`), senza cambiare il contratto
  pubblico.

## Definition of Done

Il ticket si chiude quando la base statica è cachata, solo i path sono
aggiornati per frame, il conteggio di materializzazione scende e SHA/test sono
verdi. Senza misura e SHA il ticket resta aperto.

## Esito verifica — 2026-11-22

- Nuovo test `RenderPipeline - animated path updates an overlay without
  rematerializing the static base`: 42/42 assertions.
- Su 30 frame: `scene_materialize_count == 1`, `scene_path_update_count == 30`,
  hash frame 0 != hash frame 29 (path animato non congelato).
- `chronon3d_render_graph_tests`: 470/470, 19945 assertions.
- `chronon3d_deterministic_tests`: 94/94, 11797 assertions.
- **Residuo:** non è disponibile un artifact baseline del job per il confronto
  SHA “identico al baseline”. Il ticket resta aperto secondo la sua DoD, anche
  se la regressione CPU è coperta e verde.
