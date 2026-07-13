# TICKET-PERSISTENT-CACHE-ADR-GAP — Missing ADR for dead persistent cache bridge

## Stato: OPEN (2026-07-13, prerequisite for P1-B cache removal)

## Problema

Il piano di semplificazione (punto #8) attribuisce la rimozione della cache
persistente morta ad ADR-024. Verifica documentale macchina-verificata:

- `docs/adr/ADR-024-composite-node-counter.md` tratta del **composite node
  atomic-counter hidden singleton** (DELETE + dependency injection), NON della
  cache persistente.
- Scansione completa di `docs/adr/` (24 ADR files + INDEX.md + README.md):
  **nessun ADR** documenta la rimozione del bridge executor-side che collegava
  `RenderNodeCachePolicy::persistent()` al `PersistentFramebufferStore`.
- ADR-002 menziona "cache" ma nel contesto del modello di ownership di
  `RenderRuntime` (NodeCache, FramebufferPool, CompiledGraphCache), non della
  rimozione del bridge persistent.

Il codice morto ha commenti inline (`cache_evaluator.cpp:12`, `node_runner.cpp:95`)
che dicono "persistent framebuffer cache removed (framebuffer_store →
framebuffer_pool)" ma **nessun ADR** formalizza questa decisione.

## Evidenza (machine-verified)

Codice morto confermato:

1. `persistent_framebuffer_cache_enabled_for_current_run()` in
   `src/render_graph/executor/cache_evaluator.cpp:13` — restituisce `false`
   incondizionatamente.
2. `(void)policy.persistent();` cast in `cache_evaluator.cpp:81` — risultato
   scartato.
3. `node_runner.cpp:96` — branch `if (node.cache_policy().persistent() &&
   persistent_framebuffer_cache_enabled_for_current_run())` sempre false.
4. `RenderNodeCachePolicy::persistent()` in
   `include/chronon3d/render_graph/core/cache_policy.hpp:66` — metodo ancora
   presente ma senza chiamanti reali che usano il risultato.
Candidati alla rimozione (valutare valore futuro V3 tile cache):

5. `CacheMode::FrameInvariantPersistent` + `static_persistent_cache()` factory
   in `cache_policy.hpp:33,98` — enum value + factory ancora presenti ma senza
   chiamanti reali che usano il risultato (`test_graph_optimizer.cpp:23` li
   menziona solo in un commento). V3_BLUEPRINT.md §tile cache potrebbe
   riutilizzare questo enum in futuro — valutare prima di rimuovere.

Codice VIVO da NON toccare (asse attivo diverso):

- `PersistentFramebufferStore` class (`src/cache/persistent_framebuffer_store.cpp`
  + `include/chronon3d/cache/persistent_framebuffer_store.hpp`) — test attivi
  in `tests/cache/test_persistent_framebuffer_store.cpp` + benchmark in
  `tests/bench/micro_benchmarks.cpp`.
- `RenderRuntime::framebuffer_store()` accessor — runtime-scoped ownership
  per ADR-002.
- CFB4 codec, disk backing store, config vars
  (`disable_persistent_framebuffer_cache_`, `persistent_framebuffer_cache_dir_`).
- V3_BLUEPRINT.md referenzia `PersistentFramebufferStore` per tile-level
  caching futuro.

## Decisione

 Prima di procedere con la rimozione del codice morto (P1-B del piano di
 semplificazione), serve un ADR che formalizzi:

1. La separazione tra l'asse **bridge executor-side** (morto: policy.persistent()
   → persistent_framebuffer_cache_enabled_for_current_run() → store) e l'asse
   **PersistentFramebufferStore** (vivo: CFB4 codec + disk store + runtime
   ownership).
2. La rimozione sicura del bridge morto senza influenzare lo store vivo.
3. Se `CacheMode::FrameInvariantPersistent` + `static_persistent_cache()`
   factory possono essere rimossi o se hanno un valore futuro (V3 tile cache).

L'ADR proposto è ADR-025 (prossimo slot disponibile dopo ADR-024).

## Criteri di accettazione

- [ ] ADR-025 creato in `docs/adr/ADR-025-persistent-cache-bridge-removal.md`
- [ ] ADR-025 documentato in `docs/adr/INDEX.md`
- [ ] ADR-025 distingue chiaramente bridge morto vs store vivo
- [ ] FOLLOWUP_TICKETS.md riga sintetica aggiunta (questa scheda)

## Forward-points

- `TICKET-PERSISTENT-CACHE-REMOVAL` (P1-B del piano) — esegue la rimozione
  effettiva del codice morto dopo l'accettazione di ADR-025.
- `TICKET-CACHE-MODE-PERSISTENT-REMOVAL` — valuta se rimuovere
  `CacheMode::FrameInvariantPersistent` + `static_persistent_cache()` factory.

## Cross-link

- `docs/adr/ADR-024-composite-node-counter.md` (ADR errato nel piano)
- `docs/adr/ADR-002-render-runtime-ownership.md` (ownership model, non
  bridge removal)
- `docs/adr/INDEX.md` (ADR registry — ADR-025 slot disponibile)
- `src/render_graph/executor/cache_evaluator.cpp:13,81` (codice morto)
- `src/render_graph/executor/node_runner.cpp:96` (branch sempre false)
- `include/chronon3d/render_graph/core/cache_policy.hpp:33,66,98` (policy
  methods/enum)
- `src/cache/persistent_framebuffer_store.cpp` (store VIVO — non toccare)
- `tests/cache/test_persistent_framebuffer_store.cpp` (test store VIVO)
- `docs/V3_BLUEPRINT.md:135,143` (futuro tile cache usa PersistentFramebufferStore)
