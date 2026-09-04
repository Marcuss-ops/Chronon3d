# TICKET-NODE-CACHE-KEY-COLLAPSE-ROT — NodeCacheKey collapse rotation

## Stato

DONE — re-census 2026-09-04. The historical rot condition is no longer present and the forward-points that justified keeping this ticket open have already landed.

## Priorità

P2 — CLOSED

## Problema storico

The ticket was opened to track a possible executor-module type collapse around the canonical `chronon3d::cache::NodeCacheKey`, especially while `run_node` was still embedded in `node_runner.cpp` and future extraction could have reintroduced incomplete-type or mismatched-signature behavior.

The historical failure class was a caller passing the real `NodeCacheKey` while a helper boundary could drift toward another type. The ticket explicitly required re-checking the type definition, executor includes, `emit_node_records`, `run_node`, and `cache_evaluator` after the executor decomposition landed.

## Re-census 2026-09-04

Baseline inspected before closure: `main@9d396bab2dc9d001e94ed5fb126b967042c17fe1` and the unchanged executor/cache files on the subsequent main line.

### Canonical type

`include/chronon3d/cache/node_cache.hpp` still defines the canonical `chronon3d::cache::NodeCacheKey` structure. No typedef/alias collapse to an integer type is present in the inspected path.

### `run_node`

The successor forward-point has landed: `run_node` no longer lives inline in the old runner body. It is declared in `src/render_graph/executor/node_executor.hpp` and implemented in `node_executor.cpp`.

`node_executor.hpp` explicitly includes `<chronon3d/cache/node_cache.hpp>` and its signature is:

```cpp
const ::chronon3d::cache::NodeCacheKey& key
```

This is a full-type include, not an incomplete or guessed forward declaration.

### `emit_node_records`

`src/render_graph/executor/telemetry_emitter.hpp` explicitly includes `<chronon3d/cache/node_cache.hpp>`.

Both the SQLite-enabled declaration and the compile-time no-op declaration accept the canonical key by reference:

```cpp
const chronon3d::cache::NodeCacheKey& key
```

or the namespace-equivalent `const cache::NodeCacheKey&`.

No `const int&` or alternate cache-key type remains at this boundary.

### `cache_evaluator`

`src/render_graph/executor/cache_evaluator.hpp` explicitly includes `<chronon3d/cache/node_cache.hpp>` and returns `CacheEvalResult`.

`src/render_graph/executor/execution_state.hpp` also explicitly includes the same canonical header and stores:

```cpp
NodeCacheKey key;
```

inside `CacheEvalResult`.

The cache evaluator, node execution helper and telemetry helper therefore exchange the same concrete type.

### Executor handoff

`node_runner_single_node_detail.hpp` consumes `cache_eval.key` as the single key value and passes that same value into cache evaluation/execution/telemetry boundaries. The old type-mismatch forward-point is not reproduced by the current decomposition.

### Producer census

The ticket-named producer paths were re-checked where they remain active, including the render-graph refresh/source path. They construct the canonical `cache::NodeCacheKey` and pass it through node refresh/cache APIs; no replacement primitive authority was found.

GitHub code search for this repository currently reports an incomplete index, so closure does not claim that a zero-result web index is proof of absence. Closure is based on direct inspection of the canonical definition and every rot-sensitive forward-point named by this ticket.

## Include / forward-declaration verdict

PASS.

The rot-sensitive executor headers use explicit `node_cache.hpp` inclusion where the concrete key crosses a function or stored-state boundary. No new executor-local `NodeCacheKey` declaration, typedef, or compatibility alias is required.

## Forward-point verdict

- `run_node` extraction: **LANDED**.
- full `NodeCacheKey` type at `run_node`: **PASS**.
- full `NodeCacheKey` type at `emit_node_records`: **PASS**.
- `cache_evaluator` / `CacheEvalResult` concrete key ownership: **PASS**.
- historical `const int&` mismatch class: **NOT PRESENT** in the inspected current boundaries.
- separate `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-FIX`: **NOT NEEDED**; opening one now would preserve stale debt rather than fix active code.

## Chiusura

The ticket must not remain OPEN for historical reasons. Its original risk was tied to a refactor that has already landed, and the resulting boundaries now carry the canonical type explicitly.

Any future cache-key type regression is a new concrete defect and should be handled at its actual owner, not by resurrecting this completed rotation ticket.

## Cross-link

- Canonical type: `include/chronon3d/cache/node_cache.hpp`
- Executor execution boundary: `src/render_graph/executor/node_executor.hpp`
- Cache evaluation boundary: `src/render_graph/executor/cache_evaluator.hpp`
- Executor state: `src/render_graph/executor/execution_state.hpp`
- Telemetry boundary: `src/render_graph/executor/telemetry_emitter.hpp`
- Index: `docs/FOLLOWUP_TICKETS.md`
- Current status: `docs/CURRENT_STATUS.md`
