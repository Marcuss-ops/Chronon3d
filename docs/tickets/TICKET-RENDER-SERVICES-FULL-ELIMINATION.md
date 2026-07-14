# TICKET-RENDER-SERVICES-FULL-ELIMINATION — Fully remove RenderServices from runtime header surface

> Stato: **OPEN (2026-07-14)** — forward-point from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (2nd item). Cat-3 minimal-surface.

## Problema

The P1-15 cleanup reduced `RenderServices` to an "internal struct" (per the prior chore's commit message), but the public header surface in `include/chronon3d/runtime/render_runtime.hpp` still exposes the typed accessors + the legacy `services()` accessor for backward-compat with `[[deprecated]]`:

```cpp
// include/chronon3d/runtime/render_runtime.hpp
class RenderRuntime {
public:
    // Typed accessors (CANONICAL — kept):
    [[nodiscard]] NodeCache& node_cache() noexcept;
    [[nodiscard]] Scheduler& scheduler() noexcept;
    [[nodiscard]] Resolver& resolver() noexcept;
    [[nodiscard]] Executor& executor() noexcept;
    [[nodiscard]] Pool& pool() noexcept;
    [[nodiscard]] Diagnostics& diagnostics() noexcept;

    // Legacy accessor (to be REMOVED by this ticket):
    [[deprecated("use runtime.node_cache() instead")]]
    [[nodiscard]] RenderServices& services() noexcept;
    [[nodiscard]] const RenderServices& services() const noexcept;
};
```

The `[[deprecated]]` marker is a Cat-3 anti-dup violation: it advertises a deprecated API surface that no canonical code should call. A future maintainer who sees the deprecation may think "I should use the canonical accessors" — but the deprecation itself is a maintenance burden (a marker to keep up to date, compiler warnings to suppress, etc.).

## Soluzione Confine

Delete the `[[deprecated]]` `services()` accessor + the entire `RenderServices` struct (or move it to `detail::` namespace if the internal struct is still used by the runtime itself).

Steps:
1. **Audit**: `rg "runtime\.services\(\)\."` → 0 matches in production code (canonical accessors are the only entry points)
2. **Delete** the `RenderServices` struct definition (likely in `include/chronon3d/internal/runtime/session_services.hpp` or similar internal header)
3. **Delete** the `services()` accessor + the `m_services` field from `RenderRuntime`
4. **Delete** any include of `RenderServices` from runtime source
5. **Update** any documentation that mentions the deprecated accessor (ADRs, README, etc.)

The internal struct (if still used by the runtime's implementation files) can be moved to `chronon3d::detail::` namespace — but if the P1-15 chore truly reduced it to "internal struct", the public surface removal is the only remaining work.

## Criteri di accettazione

- [ ] `rg "RenderServices"` in `include/chronon3d/` → 0 matches (or matches only inside `detail::` namespace)
- [ ] `rg "services\(\)"` in `src/` and `apps/` → 0 matches (or matches only inside `detail::` namespace)
- [ ] `[[deprecated]]` markers on `services()` accessor deleted
- [ ] `RenderServices` struct definition deleted (or moved to `detail::` namespace)
- [ ] `m_services` field deleted from `RenderRuntime`
- [ ] All 6 typed accessors (`node_cache` + `scheduler` + `resolver` + `executor` + `pool` + `diagnostics`) preserved
- [ ] 0 new public SDK symbol (the deletion is recipe-substitution, not surface-additive)
- [ ] 0 new singleton/registry/resolver/cache
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>`
- [ ] If `RenderServices` is still used internally, move to `chronon3d::detail::RenderServices` (Cat-3 anti-dup: internal detail surface)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification:
- `rg "RenderServices" include/` → confirm 0 matches (or only `detail::RenderServices` if still used)
- `rg "runtime\.services\(\)\." src/ apps/ tests/` → confirm 0 matches in production code

## Cat-3 minimal-surface

- 1 DELETE public accessor (`RenderRuntime::services()`)
- 1 DELETE public struct (`RenderServices`)
- 1 DELETE field (`RenderRuntime::m_services`)
- 1 EDIT include statements (remove `RenderServices` from runtime headers)
- 0 NEW files
- 0 NEW public SDK symbol

NO surface-additive change. Pure deletion per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (2nd item)
- **Prior chore**: commit (P1-15 `refactor(services): reduce RenderServices to internal struct`) — partial fix; this ticket completes it
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
