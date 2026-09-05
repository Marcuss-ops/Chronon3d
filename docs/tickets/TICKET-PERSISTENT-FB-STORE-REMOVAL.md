# TICKET-PERSISTENT-FB-STORE-REMOVAL — Demolizione PersistentFramebufferStore (codec CFB4)

**Stato:** REMOVED (code) — attende verifica CI same-SHA
**Priorità:** P2
**Data:** 2026-09-05
**Owner:** Core Runtime / Cache Working Session

---

## Scheda di Demolition Debt

1. **Owner** — Core Runtime / Cache Working Session.
2. **Reason** — ADR-024 aveva deprecato il solo asse per-node persistent-flag e dichiarato il codec CFB4 (`framebuffer_store`) "out of scope"; il census successivo (vedi sotto) mostra zero produttori/consumatori in produzione: `persistent_framebuffer_cache_enabled_for_current_run()` e il branch morto in `node_executor.cpp` erano già stati rimossi (step 1/3 di ADR-024). Il codec restava vivo solo come carry cost: costruito di default da ogni `RenderRuntime`, incluso nell'hot TU `node_runner.cpp`, con env vars, config surface e ~300 righe di test dedicati.
3. **Exit condition** — Zero chiamanti reali di `PersistentFramebufferStore`, `framebuffer_store()`, `has_framebuffer_store()`, `disable_persistent_framebuffer_cache*` e `CHRONON_PERSISTENT_FB_CACHE_DIR` fuori da tests/bench dedicati (condizione verificata via ripgrep prima della rimozione). Supera la scope-clarification di ADR-024 (asse CFB4), che resta come record storico.
4. **Equivalence test/gate** — Rimozione pura: nessuna semantica di rendering o cache cambia (lo store non era mai raggiunto dall'hot path). Gate: CI same-SHA (build 756 targets, suite cache/runtime/bench). Nota: la rimozione di `RenderRuntime::framebuffer_store()` / `has_framebuffer_store()` e dei setter `Config::set_*` è una rottura ABI C++ intenzionale → rigenerare/riallineare la baseline libabigail (`.github/workflows/abi.yml`) nello stesso changeset.
5. **Removal scope**
   - `include/chronon3d/cache/persistent_framebuffer_store.hpp`
   - `src/cache/persistent_framebuffer_store.cpp`, `src/cache/persistent_framebuffer_store_config.cpp`
   - Membro `m_framebuffer_store` + accessor in `RenderRuntime` (`render_runtime.hpp/.cpp`) e blocco di costruzione in `populate()`
   - `CacheConfig::disable_persistent_framebuffer_cache*`, `PathConfig` (classe rimasta vuota), setter `Config::set_*`, parsing env in `config.cpp`
   - Include in `node_runner.cpp`, `software_renderer.cpp`
   - `tests/cache/test_persistent_framebuffer_store.cpp`, sottocasi in `tests/runtime/test_render_runtime_isolation.cpp`, micro-benchmark in `tests/bench/micro_benchmarks.cpp`
   - Sync taxonomy: `AGENTS.md`, `include/chronon3d/cache/cache_taxonomy.hpp`
6. **Status** — `REMOVED` (source); chiudere solo dopo CI verde same-SHA.

## Census pre-rimozione

- `persistent_framebuffer_cache_enabled_for_current_run()` / `.persistent()` / `(void)policy.persistent();`: già assenti dal codice (ADR-024 step 1/3 atterrati).
- Chiamanti di `framebuffer_store()` / `has_framebuffer_store()`: solo test (`test_render_runtime_isolation.cpp`).
- Chiamanti di `disable_persistent_framebuffer_cache` / `persistent_framebuffer_cache_dir`: solo `config.cpp`, `render_runtime.cpp::populate()` e test.
