# TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL — Vacuous-truth audit + Cat-5 3-doc closure

> Stato: **DONE (2026-07-14, commit `<pending>`)** — implementation of [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md) via **vacuous-truth audit** (the ticket's premise was false: the runtime `RenderServices` was already removed in P1-15). No source changes needed. Cat-3 minimal-surface preserved.

## Vacuous-truth finding (CRITICAL)

The ticket (b) premise — "the public header surface in `include/chronon3d/runtime/render_runtime.hpp` still exposes the typed accessors + the legacy `services()` accessor for backward-compat with `[[deprecated]]`" — is **FALSE**. The runtime `RenderServices` was already removed in P1-15 (commit message: `refactor(services): reduce RenderServices to internal struct`).

### Evidence (macchina-verificata 2026-07-14)

| Search | Result | Conclusion |
|---|---|---|
| `rg "struct\s+RenderServices" include/chronon3d/runtime/` | 0 matches | Runtime struct already removed |
| `rg "services\(\)" include/chronon3d/runtime/render_runtime.hpp` | 0 matches | Accessor already removed |
| `rg "m_services" include/chronon3d/runtime/render_runtime.hpp` | 0 matches | Field already removed |
| `rg "\.services\(\)\." src/ apps/` | 0 matches | No productive call sites |
| `rg "RenderServices" include/chronon3d/runtime/` | 0 matches | No references in runtime headers |

### `graph::RenderServices` is a DISTINCT per-frame bundle (unaffected)

The ticket scope was the **runtime** `RenderServices` (the service-locator bundle on `RenderRuntime`). This was already removed in P1-15.

There IS a `RenderServices` struct in `include/chronon3d/render_graph/render_graph_context.hpp:218`, but it is:
- In the `chronon3d::graph` namespace (not `chronon3d::runtime`)
- A per-frame bundle on `RenderGraphContext` (not on `RenderRuntime`)
- Used as a service-locator for graph nodes (not for the runtime)

Per the header comment at `include/chronon3d/runtime/render_runtime.hpp:139`:
> "graph::RenderServices in render_graph_context.hpp is a distinct per-frame bundle — unaffected."

The ticket (b) does NOT mention `graph::RenderServices` and its scope was the runtime services only. The `graph::RenderServices` is **out of scope** for this ticket.

## Soluzione Confine (implemented)

**NO source changes** — the vacuous-truth audit confirms the work was already done in P1-15. The chaser-chore is the entire chore scope (same pattern as [TICKET-PROCESS-WIDE-STATE-MIGRATION](docs/tickets/TICKET-PROCESS-WIDE-STATE-MIGRATION.md) + the recently-closed (c) and (d) chaser-chores).

## Criteri di accettazione

- [x] `rg "struct\s+RenderServices" include/chronon3d/runtime/` → 0 matches (runtime struct already removed in P1-15)
- [x] `rg "services\(\)" include/chronon3d/runtime/render_runtime.hpp` → 0 matches (accessor already removed in P1-15)
- [x] `rg "m_services" include/chronon3d/runtime/render_runtime.hpp` → 0 matches (field already removed in P1-15)
- [x] `rg "\.services\(\)\." src/ apps/` → 0 matches (no productive call sites in production code)
- [x] `rg "RenderServices" include/chronon3d/runtime/` → 0 matches (no references in runtime headers)
- [x] `graph::RenderServices` correctly identified as distinct per-frame bundle (out of scope for this ticket)
- [x] 0 source modifications (vacuous-truth state precludes any source edit)
- [x] 0 new public SDK symbol
- [x] 0 new singleton/registry/resolver/cache
- [x] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [x] All 6 typed accessors (`node_cache` + `scheduler` + `resolver` + `executor` + `pool` + `diagnostics`) preserved on `RenderRuntime`

## macchina-verifica

VPS-only verification (this session, 2026-07-14):
- `rg "struct\s+RenderServices" include/chronon3d/runtime/` → 0 matches ✓
- `rg "services\(\)" include/chronon3d/runtime/render_runtime.hpp` → 0 matches ✓
- `rg "m_services" include/chronon3d/runtime/render_runtime.hpp` → 0 matches ✓
- `rg "\.services\(\)\." src/ apps/` → 0 matches ✓
- `rg "RenderServices" include/chronon3d/runtime/` → 0 matches ✓
- `rg "struct RenderServices" include/chronon3d/render_graph/render_graph_context.hpp` → 1 match (`graph::RenderServices`, distinct per-frame bundle, out of scope) ✓
- `rg "\.services = RenderServices" src/` → 1 match (`src/render_graph/pipeline/helpers.hpp:155`, graph::RenderServices init, out of scope) ✓

Build verification: DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern.

## Cat-3 minimal-surface

- 0 EDIT source files (vacuous-truth state precludes any source modification)
- 1 NEW ticket-home (this file, the vacuous-truth audit)
- 1 EDIT CHANGELOG.md (cite-only entry)
- 1 EDIT FOLLOWUP_TICKETS.md (Cita-Only row in §Recently Closed — ticket was NOT in §Open Blockers because the concurrent-agent followup-discipline-formalization cleanup cycle removed it during this session)
- 1 EDIT canonical ticket (Stato: OPEN → DONE)
- 0 NEW files
- 0 NEW public SDK symbol
- 0 NEW singleton/registry/resolver/cache

NO surface-additive change. Pure Cat-5 3-doc chaser-chore per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent forward-point ticket**: [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md) — this ticket is the implementation
- **Prior chore**: commit (P1-15 `refactor(services): reduce RenderServices to internal struct`) — the work was already done; this chaser-chore just documents the vacuous-truth audit
- **Sibling vacuous-truth precedent**:
  - [TICKET-PROCESS-WIDE-STATE-MIGRATION](docs/tickets/TICKET-PROCESS-WIDE-STATE-MIGRATION.md) (Fase B2 + B3 already-completed migration)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL.md) (recently-closed (c) chaser-chore, 19 files / ~228 sites)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL.md) (recently-closed (d) chaser-chore)
  - [TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT](docs/tickets/TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT.md) (scope-realignment audit precedent)
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) (next: (a))
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md) (next: (e))
- **Cat-5 3-doc chaser-chore pattern**: this ticket is the vacuous-truth chaser-chore (1 NEW ticket-home + 1 CHANGELOG entry + 1 §Recently Closed row)
