# TICKET-ROT-STRUCTURAL-FIX — Namespace architecture structural fix forward-point (ADR)

## Stato: OPEN (2026-07-21, this commit opens as canonical forward-point for ADR on namespace architecture)

**Predecessor**: [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) (CLOSED-WONTFIX-2 2026-07-21 per user Path C, with full §honest-discipline disclosure).

**Predecessore v6-residual** (anch'esso CLOSED-SUPERSEDED in this commit per non-catena-canonical): [TICKET-V6-RESIDUAL-ROTOLOGY](TICKET-V6-RESIDUAL-ROTOLOGY.md).

**Scope**: produce an Architecture Decision Record (ADR) per risolvere il rotology post-baseline (`7eb5c2ba` baseline cert 11/11 verde non e' rotto dal rotology 356 errori HEAD-current; rotology emerged in subsequent commits, requiring structural not symptomatic fix). Implementation deferred to ADR-decided follow-up chore.

## §Why a forward-point (Path C rationale)

Il rotology systemico osservato in `ninja -C build/chronon/linux-content-dev all` (356 errori / 175 rot-patterns / 27+ file scoperti post-baseline `7eb5c2ba`) e' REALE ma non rompe il baseline 11/11 verde certificato:

1. **Cert baseline 11/11 a `7eb5c2ba`** è documented in `docs/baselines/main-7eb5c2ba-baseline.md` e include tutti i target attualmente build-breaking (registry, text_health_tests, backend_software, effects) — il rotology post-baseline è emerso dopo il commit cert.
2. **Cat-3 anti-dup**: closure as WONTFIX con disclosure onesta + forward-point ADR-grade evita `cascading partial-fix iterations on main` (cattura V1→V5, V6→V7) che introducono blast-radius re-iteration su include-graph senza convergence.
3. **§Post-push SHA-selfcheck invariant**: Path D ha richiesto push di build parzialmente red (V6 31 file / 404 LoC), disclosure onesta. Path C preferisce evitare partial-build-push via ADR forward-point.

## §Resolved predecessor state (chiusura)

- [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md): CLOSED-WONTFIX-2 (4 iterazioni di apertura/chiusura in questa sessione, canonical cronaca preservata).
- [TICKET-V6-RESIDUAL-ROTOLOGY](TICKET-V6-RESIDUAL-ROTOLOGY.md): CLOSED-SUPERSEDED (Path D forward-point chiuso per catena-canonical con questo ticket).
- [TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md): CLOSED-SUPERSEDED (5-strategy matrix documentata li', inherit in questo ticket §Decision Matrix).

## §Rotology blast radius (richiesto per ADR context)

Baseline `7eb5c2ba`: 11/11 PASS (`docs/baselines/main-7eb5c2ba-baseline.md`).
HEAD-current (`ec6d6aab` pre-revert / `323fe2ce` baseline): `ninja all` mostra **356 errori / 175 rot-patterns** in 27+ file / 6 OBJECT targets. Path B Op1 V6 attempted fix achieved 52.5% reduction (356→~169) ma con cascade-include-graph rotology che revela nuove symbol-missing on transitive re-compile, establishing symptomatic-only nature del fix.

**Top-5 post-V6 hotspots** (per Strategia-context):
1. `src/registry/text_preset_factories_emphasis.cpp` — 62 errors (style structural ambiguity from `namespace chronon3d::registry::register_helpers_internal::factory_emphasis { ... }` C++17 nested-syntax)
2. `include/chronon3d/render_graph/render_graph_context.hpp` — 32 errors (transitive include-graph rot da V6 modification di `render_runtime.hpp`)
3. `include/chronon3d/timeline/composition.hpp` — 24 errors
4. `include/chronon3d/math/projector_2_5d.hpp` — 20 errors
5. `src/registry/text_preset_internal_helpers.hpp` — 18 errors

**Symbols missing** (richiesti V7+ residue dal V6/V7 attempts): `backends, framebuffer_clear_parallel, FramebufferArena, ResolvedCamera, ResolvedLayer, TextRunShape, hash_text_run_shape, prefetch, project_layer_2_5d, telemetry, from_mat4` (14+ symbols total).

## §Architectural Decision Strategies (5 options + recommendation)

Vedi [TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md) §Architectural decisions to consider per il catalogo completo delle 5 strategie A-E. Sintesi:

- **Strategy A** — Project-wide explicit `::chronon3d::` qualification: ~150-200 LoC across 27+ file, symptomatic ma non root-cause.
- **Strategy B** — Convert `namespace chronon3d::subns { ... }` → proper nesting (root-cause C++17 nested-ns interaction): ~27-50 file declaration changes + close-brace rebalancing.
- **Strategy C** — `using namespace ::chronon3d;` at rot sites: ADL-pollution risk (anti-pattern).
- **Strategy D** — Selective header re-include to flatten dependency graph: variable blast radius.
- **Strategy E** (combined) — Strategy B for scope-lookup rotology + Strategy D for missing-include rotology + Strategy A for residual orphan-symbol rotology. **Recommended per Path D V6 empirical evidence** (Op1 qualification funziona ma cascada; Op2 nested-ns deve essere applicata).

## §Decision protocol

1. Open `docs/adr/ADR-019-namespace-architecture-revisited.md` per `docs/DOCUMENTATION_GOVERNANCE.md` §adr.
2. §Context: rotology blast radius 356 / 5 hotspots / 14+ symbols / V6 52.5% partial + cascade failures.
3. §Decision: scegli combinazione canonica (Strategy E default, o A or B-only se Context argomenti).
4. §Consequences: blast radius, file count, LoC, build verification, downstream macchina-verifica WBH.
5. §Alternatives: brief justification per 4 strategie scartate.
6. Implement choice via downstream chore(s) per AGENTS.md §Fare PR piccole e mirate (3 area-grouped chores: B per scope-lookup, D per include-flatten, A per residual).

## §Closure criterion

- ADR record pubblicato in `docs/adr/ADR-NNN-namespace-architecture-revisited.md`.
- Implementation chore(s) merged on `main` per ADR decision.
- `ninja -C build/chronon/linux-content-dev all` exit 0 (full build clean).
- WBH macchina-verifica 11/11 baseline suite verde post-implementation.
- Forward-point TICKET-ROT-STRUCTURAL-FIX chiuso come DONE.

## §References

- [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) — predecessor (CLOSED-WONTFIX-2 2026-07-21 con full §honest-discipline disclosure)
- [TICKET-V6-RESIDUAL-ROTOLOGY](TICKET-V6-RESIDUAL-ROTOLOGY.md) — Path D forward-point (CLOSED-SUPERSEDED per canonical-chain-discipline)
- [TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md) — earlier forward-point (CLOSED-SUPERSEDED, 5-strategy matrix inherit)
- [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — original upstream 5-file rot ticket (predecessore iniziale)
- `docs/baselines/main-7eb5c2ba-baseline.md` — 11/11 baseline cert (non rotto da rotology post-baseline)
- `/tmp/baseline_rot.log` + `/tmp/ninja_all_rot.log` + `/tmp/v7_residual_symbols.txt` — rotology evidence base
- AGENTS.md §Fare PR piccole e mirate + §Docs canonical update discipline rule + §Post-push SHA-selfcheck invariant + §honest-discipline
