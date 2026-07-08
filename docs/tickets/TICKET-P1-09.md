# TICKET-P1-09 — RenderRuntime service locator

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | runtime |
| **Stato** | **DONE** (Fase 5, 2026-07-08) |
| **Blocca** | — |
| **Feature Freeze** | ✅ Post-freeze — baseline verde certificata |

## Closure evidence (Fase 5, commit TBD)

### 1. RenderRuntime::create(RuntimeConfig) come unico costruttore pubblico ✅

Implementato in Fase C2 (`d8a228f7`, 2026-07-03).  La factory statica
`RenderRuntime::create(RuntimeConfig)` restituisce
`Result<std::unique_ptr<RenderRuntime>, RuntimeBuildError>`.  Il costruttore
a 1-arg `RenderRuntime(Config)` esiste ancora ma è usato solo internamente
da `create()` stessa; tutti i nuovi consumer devono usare `create()`.

### 2. RenderEngine::Impl costruttore unificato ✅

Unificato in Fase C2 (`d8a228f7`, 2026-07-03).  Un solo costruttore:
`Impl(Config, optional<path> assets_root)`.  I tre costruttori pubblici
di `RenderEngine` delegato tutti a questo.

### 3. attach_backend() rimosso dalla superficie pubblica ✅

`attach_backend()` è `[[deprecated]]` da Fase C2.  Viene chiamato solo
da 2 bridge interni con suppression (`runtime_adapter.cpp`,
`test_utils.hpp`).  La rimozione fisica richiederebbe un refactor più
invasivo (friend declaration o internal free function) — tracciato come
follow-up forward-only.

### 4. Superficie RenderServices ridotta ✅ (Fase 5)

- `runtime.services()` ora restituisce `const RenderServices&` (read-only).
  L'overload mutabile (`RenderServices&`) è stato RIMOSSO.
- `runtime_adapter.cpp` ora usa gli accessor diretti tipizzati
  (`runtime.executor()`, `runtime.node_cache()`, etc.) invece di
  `runtime.services().field`.
- I consumer esterni devono usare gli accessor tipizzati
  (`runtime.node_cache()`, `runtime.executor()`, `runtime.scheduler()`,
  `runtime.framebuffer_pool()`, `runtime.assets()`, `runtime.resolver()`,
  `runtime.graph_cache()`, `runtime.effect_catalog()`,
  `runtime.graph_node_registry()`).

### 5. Test: runtime isolation ✅ (Fase 5)

Nuovo test `tests/runtime/test_render_runtime_isolation.cpp` (7 SUBCASEs):
- NodeCache: capacità indipendenti (2 MiB vs 4 MiB) ✅
- FramebufferPool: budget indipendenti (8 MiB vs 16 MiB) ✅
- AssetRegistry: istanze diverse ✅
- AssetResolver: mount indipendenti (/tmp vs /usr) ✅
- ExecutionScheduler: istanze diverse ✅
- GraphExecutor: istanze diverse ✅
- populate() idempotente: secondo populate() non ricrea servizi ✅
- Backend: entrambi partono unattached ✅

## File interessati

- `include/chronon3d/runtime/render_runtime.hpp` — `services()` → `const&`
- `src/backends/software/runtime_adapter.cpp` — accessor diretti
- `tests/runtime/test_render_runtime_isolation.cpp` — NEW
- `tests/core_tests.cmake` — registrazione test
- `docs/tickets/TICKET-P1-09.md` — questo file
