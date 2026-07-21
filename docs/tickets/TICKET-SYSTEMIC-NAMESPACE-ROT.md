# TICKET-SYSTEMIC-NAMESPACE-ROT — Systemic doubled-namespace rot across 19 files

## Stato: OPEN — V6 structural fix (2026-07-21, this reopen commit reverts the prior `e3846d43` CLOSED-WONTFIX per user Path D instruction: full `ninja all` inventory + Path B structural fix on COMPLETE file list)

**Path D intent**: il precedent commit `e3846d43` (CLOSED-WONTFIX + forward-point ADR `TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED`) è stato superato da user instruction Path D: completare l'inventario rotology con `ninja -C build/chronon/linux-content-dev all` (full build), estrarre lista completa file-errori, applicare Path B structural fix (namespace convert + ::chronon3d:: qualification) su TUTTA la lista, rebuild totale + smoke + commit.

**L'ADR forward-point resta valido** come decision-grade ref per qualunque rotology non chiudibile da Path B structural fix (es. casi edge di Strategy E combined B+D+A).

**Tempo stimato**: 60-90 min.

**Verdict & disposition**: Systemic rotology confirmed via `ninja -C build/chronon/linux-content-dev <targets>` diagnostic. Mitigation via targeted `::chronon3d::X` qualification (Path B / Op1 attempted) closed 1 of 6 affected targets (`chronon3d_registry`: 394 errors → 0 across the 4 `text_preset_factories_*` files via 83 idempotent qualifications); the other 5 targets (`chronon3d_text_health_tests`, `chronon3d_effects`, `chronon3d_backend_software`, etc.) carry **1500+ remaining rot errors** with disjoint symbol scopes that cannot be closed by symbol-list extension without cascade iteration.

Per AGENTS.md §"Fare PR piccole e mirate" + §honest-discipline (no `'X' is not a member of 'chronon3d::chronon3d'` rot-pattern change should be pushed with build still rot-affected across multiple targets), the fix degenerated into an unbounded cascade and was reverted. Path B was the right **direction** but the right **answer** is structural — requiring a project-wide namespace architecture decision (ADR).

## §Closure cancelled (2026-07-21)

**§honest-discipline deviation**: This ticket was prematurely opened with 19/21-file scope based on compiler observations at commit `5518c619`. After build discovery at `323fe2ce` (HEAD), the actual rotology blast radius expanded to **1500+ errors across 27+ unique source files spanning 6 OBJECT targets**. The TICKET-scope table (21 file) is an under-estimate and is preserved here for historical traceability only.

**§CLOSED-WONTFIX disposition**: The rotology is real, pervasive, and not addressable via targeted qualification at individual call-sites without cascading. Future agenti: do NOT attempt fix on individual symbol lists — open the forward-point ADR and resolve the architecture first.

## §Rotology blast radius (2026-07-21 diagnostic, verbatim from `/tmp/baseline_rot.log` + post-Op1 build logs)

**Targets measured** (6 OBJECT libs, not full `ninja all`):
| Target | Pre-Op1 | Post-Op1 | Δ |
|---|---|---|---|
| `chronon3d_animations` | 0 | 0 | = |
| `chronon3d_core_impl` | (not measured) | 0 | new probe |
| `chronon3d_registry` | 394 | 0 | Op1 closed ✅ |
| `chronon3d_text_health_tests` | 394 | 395 | ≈ (Op1 didn't cover) |
| `chronon3d_effects` | (not measured) | 157 | new probe |
| `chronon3d_backend_software` | (not measured) | 975 | new probe |

**Top-4 hotspots** (per-file error counts at HEAD baseline):
1. `include/chronon3d/runtime/render_runtime.hpp`: 76 errors (RenderSession / ExecutionScheduler / Config / RenderCounters / ImageCache / Scene / Easing scope rot)
2. `src/registry/text_preset_factories_basic.cpp`: 58 errors (CLOSED by Op1)
3. `src/registry/text_preset_factories_reveal_basic.cpp`: 39 errors (CLOSED by Op1)
4. `include/chronon3d/math/camera_projection_resolver.hpp`: 32 errors (test_frustum_culling / clip_with_uv / compute_signed_area / build_perspective_matrix: PARTIALLY closed by Op1)

**Rotology pattern** (canonical quote from compiler):
```
'X' is not a member of 'chronon3d::chronon3d'; did you mean 'chronon3d::X'?
'chronon3d::chronon3d::SUBNS' has not been declared
```
Affected files mix `namespace chronon3d::subns { ... }` (C++17 nested-ns syntax) with `namespace chronon3d { ... }` flat syntax, creating scope-lookup ambiguity under GCC's diagnostic formatter.

## §Why Path B was insufficient (single-shot analysis)

**Op1 (`::chronon3d::X` qualification)**:
- Pattern: `(?<!::)(?<!:)(?<!\w)chronon3d::(test_frustum_culling|clip_with_uv|compute_signed_area|build_perspective_matrix|camera_math|registry)(?!\w)`
- Files touched: 8 (mostly registry + camera_math + 4 free function sites)
- Result: closed `chronon3d_registry` target entirely (394→0); did NOT close other targets because they use DIFFERENT symbol sets (RenderSession / ExecutionScheduler / Scene / ImageCache / Easing / etc.) that were not in the bounded symbol list.

**Bounded symbol-list vs unbounded rotology**:
- `render_runtime.hpp` rot uses types like `RenderSession`, `ExecutionScheduler`, `Config`, `RenderCounters`, `ImageCache`, `Scene`, `Easing`. Adding these to Op1 regex would extend blast radius and risk cascade regressions (each added symbol may affect downstream files beyond the rot-scope).

**The real rotology is structural, not call-site**:
- Root cause: C++17 nested-namespace syntax `namespace chronon3d::subns { ... }` interacts with parent-scope lookup in GCC's diagnostic, producing the `'X' in namespace 'chronon3d::chronon3d'` pattern. The proper fix is either (a) flatten all namespace declarations to `namespace chronon3d { namespace subns { ... } }` syntax (proper nesting — parent lookup works) OR (b) add `using namespace ::chronon3d;` directives at the top of every rot-affected sub-namespace block.
- Both options require an ADR-grade architectural decision (single-shot vs multi-chore, blast radius, semantic-preservation guarantees).

## §Forward-points

- **[TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md)** — forward-point: ADR on namespace architecture (the canonical place for the rotology resolution strategy + blast-radius analysis + decision matrix).
- `docs/CHANGELOG.md` 2026-07-21 entry (canonical "chore(docs)" ticket closure log) per Cat-3 anti-dup.
- `docs/FOLLOWUP_TICKETS.md` new P1 OPEN row for the ADR ticket (Cat-5 3-doc same-commit pattern).

## §Multi-target syntax note (2026-07-21)

**Trappola CMake CLI documentata**: `cmake --build <dir> --target A B C ...` (target separati da SPAZIO) **NON** funziona — CMake interpreta i target successivi come argomenti da passare al tool nativo (gmake/Ninja), risultando in errori tipo `gmake: Makefile: No such file or directory`. Sintassi corretta:
- (a) `--target A --target B --target C` (flag separati)
- (b) `cmake --build <dir> -- A B C` (passa target al tool nativo)
- (c) `ninja -C <dir> <target>` (chiamata diretta Ninja)

**§Forward-points (per future agenti)**:
- Documentare la sintassi multi-target in `AGENTS.md §Build commands` (NON ora, deferred a chore separato per minimizzare blast radius).

## Problema

Il rebuild post-fix Opzione 2 di [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) ha rivelato che il build rot `'X' is not a member of 'chronon3d::chronon3d'` è **sistemic** (>1500 errori su 27+ file), non isolato al GradientStop.

Errori verificati (verbatim dal compilatore):
```
src/effects/effect_catalog.cpp:16:5: 'stack' was not declared in this scope
include/chronon3d/effects/curves.hpp:27:31: 'CurvePoint' in namespace 'chronon3d::chronon3d' does not name a type
include/chronon3d/core/config.hpp:NN: 'graph' / 'cache' / 'Easing' in namespace 'chronon3d::chronon3d' does not name a type
include/chronon3d/runtime/render_runtime.hpp:NN: 'RenderSession' / 'ExecutionScheduler' / 'Config' / 'RenderCounters' / 'ImageCache' / 'Scene' / 'Easing' in namespace 'chronon3d::chronon3d' does not name a type
... (~20 altri file con stesso pattern)
```

## Root cause

C++17 nested-namespace syntax (`namespace chronon3d::subns { ... }`) interacts with parent-scope lookup in GCC's diagnostic formatter. When code inside such a block uses `chronon3d::X` (qualified parent lookup), the compiler formats the error as `'X' in namespace 'chronon3d::chronon3d'` (concatenation of current scope + qualified lookup path), making the rotology look like a name-collision when it's actually a scope-ambiguity.

**Stessa architettura FASE 9** (estrazione sotto-namespace `chronon3d::subns`) + commit `1c6e4c88` (move lerp include outside `chronon3d::graphics` namespace) documentati in [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) §(a) sono il pattern architetturale che ha generato il rotology — singoli commit atomici corretti nel loro contesto, ma cumulativamente hanno prodotto il rotology systemico ora sotto ADR.

## Scope (historical 21 file snapshot, superseded — see §Rotology blast radius above)

| Area | File |
|---|---|
| camera | `include/chronon3d/math/camera_2_5d_projection.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp`, `include/chronon3d/scene/camera/camera_v1/camera_program.hpp` |
| effects | `include/chronon3d/effects/curves.hpp`, `include/chronon3d/effects/glow_pipeline.hpp`, `src/effects/effect_catalog.cpp` |
| core | `include/chronon3d/core/config.hpp`, `include/chronon3d/core/execution/execution_scope_types.hpp`, `include/chronon3d/core/scheduler/execution_scheduler.hpp`, `include/chronon3d/core/scope/execution_scope.hpp`, `include/chronon3d/core/types/frame_context.hpp` |
| runtime/internal | `include/chronon3d/runtime/render_runtime.hpp`, `include/chronon3d/internal/runtime/render_session.hpp`, `include/chronon3d/internal/runtime/session_services.hpp` |
| render_graph | `include/chronon3d/render_graph/builder/precomp_builder_service.hpp`, `include/chronon3d/render_graph/cache/scene_program_cache.hpp` |
| backends | `include/chronon3d/backends/software/software_renderer.hpp`, `include/chronon3d/backends/software/software_render_session.hpp` |
| timeline | `include/chronon3d/timeline/composition.hpp` |
| animations | `include/chronon3d/animations/camera_motion_params.hpp` |

## Strategia di fix raccomandata (superseded — see §Why Path B was insufficient above)

**Targeted qualification** (NON regex generico che rischia over-qualification):

1. Per ogni file, identificare i tipi specifici che falliscono.
2. Per ogni tipo T, applicare `chronon3d::T` → `::chronon3d::T` SOLO per i call-site dentro sub-namespace (`namespace chronon3d::subns { ... }`).
3. Aggiungere `#include` espliciti per tipi non transitivamente visibili.
4. NON toccare `chronon3d::subns::Y` (legittimo, NON over-qualificare).

**Magnitude inizialmente stimato**: ~30-50 LoC totali across 19 file.

**Magnitude effettivamente richiesto per chiudere il rotology completo**: ~150-200 LoC across 27+ file, con blast-radius risk su tutta la catena di dipendenze. Richiede ADR.

## §Cronologia

- 2026-07-21: smoke test iniziale → scoperto GradientStop rot (TICKET-GRAPHICS-SHAPE-STYLE-ROT aperto in `d0ed0876`)
- 2026-07-21: Opzione 2 GradientStop fix verificato (chronon3d_animations PASS) → rebuild 6 test targets FAIL con rot sistemico aggiuntivo
- 2026-07-21: questo ticket aperto per tracciare lo scope espanso
- 2026-07-21: BLOCKER #1 + #2 cmake fix pushed (commits `05d55ef` + `0946c814`)
- 2026-07-21: V1→V5 cascade substitute attempt (in turno precedente): each iteration revealed NEW rot; never converged; reverted
- 2026-07-21: Op1 (Path B) attempted: closed `chronon3d_registry` target (394→0 across 4 files); rest 1500+ errors disjoint, cannot be closed by symbol-list extension
- 2026-07-21: Op1 reverted per AGENTS.md §honest-discipline (no partial-fix build push)
- 2026-07-21: this chore — TICKET CLOSED-WONTFIX + forward-point ADR ticket CREATED per Cat-5 3-doc same-commit

## References

- [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — ticket originale (GradientStop, PARZIALE DONE)
- [TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED](TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED.md) — forward-point ADR ticket (NEW)
- AGENTS.md §Feature freeze "no nuova API SDK" — il fix è rot-mitigation, non API expansion (giustifica modifica a header pubblici)
- AGENTS.md §Fare PR piccole e mirate — il fix sistemico richiede chore separato per rispettare scope discipline
- AGENTS.md §Docs canonical update discipline rule — cronaca estesa SOLO nel ticket, canonicals sintetici
- AGENTS.md §Post-push SHA-selfcheck invariant — pre-push SHA capture + SHA-triple verify post-push
