# TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE — Incomplete-type rot chain in render_graph executor

## Stato: CLOSED (2026-07-13, rot-cascade resolved upstream at commit `4791e98b` — `text_bbox_reporter.hpp` transitive include in `src/render_graph/executor/execution_state.hpp` covers `cache::FramebufferPool` visibility; canonical `framebuffer_pool.hpp` path is in `include/chronon3d/cache/framebuffer_pool.hpp`; local Phase B-2 fix superseded)

## Problema

Dopo l'applicazione del fix cat-3-minimal a 3 `};` → `}};` in
`src/registry/text_preset_factories_reveal_selectors.cpp`
(forward-point TICKET-TEXT-PRESET-SELECTORS-BRACE), Stage 2
(`cmake --build build/manual-test --target chronon3d_core_tests`)
fallice NON agli errori di brace miss ma a un nuovo layer di rot
in `src/render_graph/executor/node_runner.cpp` +
`src/render_graph/executor/telemetry_emitter.hpp` +
`include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp`.

Errore type-cache bubble-up osservato dal compilatore:

```
src/render_graph/executor/telemetry_emitter.hpp:35 (inline stub del
  #else branch): 'NodeCacheKey' in namespace 'chronon3d::graph::cache'
  does not name a type
include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp:38:
  'NodeCacheKey' in namespace 'chronon3d::graph::cache' does not name a
  type (forward declaration shadow)
src/render_graph/executor/node_runner.cpp:26 (param signature di
  run_node(...)): analogo.
```

Pattern: la lookup di `cache::NodeCacheKey` E `cache::FramebufferPool`
all'interno di `namespace chronon3d::graph { ... }` si risolve
PRIMA a `chronon3d::graph::cache::` (inner namespace forward-declared in
`render_graph_context.hpp`/`compiled_frame_graph.hpp`/altri headers
del graph namespace) — che contiene solo forward declarations
(`class NodeCacheKey;` `class FramebufferPool;`) SENZA la definizione
completa. La definizione completa vive in
`chronon3d::cache::NodeCacheKey` (definito in
`include/chronon3d/cache/node_cache.hpp`) e idem per FramebufferPool.

Erreurs secondarie cascaded:
- "invalid initialization of reference of type 'const int&' from
  expression of type 'chronon3d::cache::NodeCacheKey'" — quando il
  compilatore segnala temporaneamente la firma della funzione
  consumatrice con il parametro forward-declared
  NodeCacheKey-promoted-to-int per via di un'incomplete-type error
  recovery.
- Namespace mismatch su FramebufferPool pointer (rilevato dalla
  basher summary ma il fix può essere unified).

## Decisione di riparazione (next-session cycle)

Approccio ibrido:
1. **Header-order fix**: in ogni header che forward-declares
   `chronon3d::graph::cache::NodeCacheKey` o `FramebufferPool`,
   aggiungere `#include <chronon3d/cache/node_cache.hpp>` (e
   `framebuffer_pool.hpp`) PRIMA del forward declaration, così che il
   compilatore veda la definizione completa invece di accettare il
   forward-decl come shadow.
2. **Opzionale (canonical tightening)**: rimuovere i forward-decl
   ridondanti a favore del `#include` diretto dove il tipo è utilizzato
   come parametro completo (non come puntatore o riferimento opaco).
3. **Cross-link investigation**: trovare OGNI forward-declaration di
   `chronon3d::graph::cache::NodeCacheKey` tramite
   `grep -rE "namespace chronon3d::graph::cache" include/ src/` per
   mappare la rot a tutti i redations-affected headers.

## Criteri di accettazione per chiusura ticket

1. Stage 2 (`cmake --build build/manual-test --target
   chronon3d_core_tests`) deve passare senza errori incomplete-type
   in `node_runner.cpp` o `telemetry_emitter.hpp` o
   `compiled_frame_graph.hpp`.
2. ZERO forward declarations di `chronon3d::graph::cache::NodeCacheKey`
   o `::FramebufferPool` che siano poi shadowed dalla definizione
   canonica `chronon3d::cache::*` — ogni forward-decl redundante va
   o rimosso o accompagnato dalla include canonica.
3. `grep -rE "namespace chronon3d::graph::cache" include/ src/` deve
   restituire 0 hits post-fix (il namespace shadow viene completamente
   eliminato in favore del path canonico).
4. macchina-verifica deferred a working build host per AGENTS.md
   §honest-limitation.

## Forward-points & cross-link

- **Predecessore onion-peel**: TICKET-TEXT-PRESET-SELECTORS-BRACE
  (3 brace rotations upstream — risolte in working tree, defer alla
  session che chiude questo rot).
- **AGENTS.md §Post-push SHA-selfcheck invariant applicato**:
  bisogna verificare che il push del chore sia atomico (cioè questo
  rot + braccia-fix + qualsiasi-altro rot del cascade sono atomic
  commits pre-push di una sola `tools/wrap_push.sh origin main`).
- **TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT** è il 4-commit cascade
  precedent per gestire onion-peel rot atomici (consultare per il
  pattern).

## Origine

Ticket aperto durante Azione 18 chore cycle (regression lock per il
silent failure di `content/animation_compositions.cpp::anim_typewriter`)
quando l'agent 2 ha run una full-rebuild dopo il brace-fix rot-1 e
ottenuto un secondo layer di errori rot-specific in `node_runner.cpp` +
`telemetry_emitter.hpp` + `compiled_frame_graph.hpp`. Questo rot è il
SECONDO strato del onion-peel cascade, dopo il primo layer risolto
da TICKET-TEXT-PRESET-SELECTORS-BRACE.

Il rot della full-rebuild è in `src/render_graph/executor/node_runner.cpp`
(riga 26 — param signature): "the compiler resolves `cache::NodeCacheKey`
all'interno di chronon3d::graph namespace al forward-declared
`chronon3d::graph::cache::NodeCacheKey` invece di risalire al canonical
`chronon3d::cache::NodeCacheKey`". Senza fix, Stage 2 BUILD rimane
non-verde.
