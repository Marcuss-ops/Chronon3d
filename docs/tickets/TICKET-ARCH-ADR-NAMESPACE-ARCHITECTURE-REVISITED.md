# TICKET-ARCH-ADR-NAMESPACE-ARCHITECTURE-REVISITED — Architectural decision on namespace structure to close systemic rotology

## Stato: OPEN (forward-point, 2026-07-21)

**Predecessor**: [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) (CLOSED-WONTFIX 2026-07-21, blast-radius 1500+ errors across 27+ files / 6 OBJECT targets).

**Scope**: produce an Architecture Decision Record (ADR) that resolves the rotology by choosing ONE canonical namespace architecture strategy. Implementation deferred to follow-up chores once decision is recorded.

## §Context

The codebase has accumulated, across multiple coherent FASE-9 sub-namespace extraction commits, a C++17 nested-namespace syntax pattern (`namespace chronon3d::subns { ... }`) that interacts with parent-scope lookup under GCC's diagnostic formatter to produce the `'X' in namespace 'chronon3d::chronon3d'` rotology pattern.

At `HEAD=323fe2ce`:
- **1500+ compile errors** across 27+ unique source files
- **6 OBJECT targets affected**: `chronon3d_text_health_tests` (395 errors), `chronon3d_backend_software` (975 errors), `chronon3d_effects` (157 errors), `chronon3d_animations` (0), `chronon3d_core_impl` (0), `chronon3d_registry` (394→0 closed via Op1)
- The `chronon3d_registry` closure proves the rot is solvable per-target via targeted qualification, but the disjoint symbol scopes across the remaining 5 targets prevent any single-shot fix.

**Past attempt (Path B / Op1)**: closed `chronon3d_registry` (394→0 across 4 `text_preset_factories_*.cpp` files via 83 idempotent `::chronon3d::X` qualifications). Reverted because the same approach did NOT close the other 5 targets and the cumulative blast-radius exploded.

## §Architectural decisions to consider

### Strategy A — Project-wide explicit `::chronon3d::` qualification
- **Description**: For every cross-namespace call-site that uses `chronon3d::X` from inside a sub-namespace, add the leading `::` to force global lookup.
- **Blast radius**: ~150-200 LoC across 27+ files, all headers are public SDK API (modifies `include/chronon3d/**/*.hpp`).
- **Risk**: each added qualification can cascade (introduce rot elsewhere); bounded symbol-list expansion degenerates into V6, V7, ... iterations.
- **Pro**: minimal semantic change; preserves existing FASE-9 sub-namespace architecture.
- **Con**: addresses SYMPTOM not ROOT CAUSE; future contributors will continue producing the same rotology.

### Strategy B — Convert `namespace chronon3d::subns { ... }` → `namespace chronon3d { namespace subns { ... } }`
- **Description**: Replace C++17 nested-namespace syntax with proper nesting at every rot-affected file. Parent lookup works natively; the `'X' in namespace 'chronon3d::chronon3d'` pattern disappears.
- **Blast radius**: ~27-50 file edits (one declaration change per file), close-brace rebalancing required.
- **Risk**: requires careful close-brace matching (already prototyped in `/tmp/rot_ns_convert.py`).
- **Pro**: addresses ROOT CAUSE (C++17 syntax interaction); future contributors automatically benefit.
- **Con**: requires build-system consistency check (some files may use the new syntax correctly — those must be left alone).

### Strategy C — `using namespace ::chronon3d;` at every rot site
- **Description**: Add a `using namespace ::chronon3d;` directive at the top of every rot-affected sub-namespace block. Resets parent lookup to global.
- **Blast radius**: ~1 line per rot file (~27+ files).
- **Risk**: pollutes local scope with all `chronon3d::*` names; ADL ambiguities may emerge.
- **Pro**: minimal LoC; preserves namespace architecture verbatim.
- **Con**: ADL pollution is a known anti-pattern; AGENTS.md §Feature freeze "no new singleton/registry/resolver/cache" precedent suggests minimizing using-directives.

### Strategy D — Selective header re-include to flatten dependency graph
- **Description**: Add missing `#include <chronon3d/X.hpp>` in sub-namespace blocks where the rotology arises from name-not-in-scope (vs scope-lookup rotology).
- **Blast radius**: variable (1-line each in worst case; could cascade if headers are missing across the sub-namespace graph).
- **Risk**: header include-ordering pathologies may emerge; transitive re-includes can break IWYU.
- **Pro**: addresses ROOT CAUSE where it IS a missing-include rotology; preserves namespace architecture.
- **Con**: requires distinguishing scope-lookup rotology from missing-include rotology per site.

### Strategy E (recommended for ADR) — Combined approach
- **Description**: Strategy B (convert nested-ns syntax) for files where the rotology is structural (parent-lookup ambiguity) + Strategy D (missing-include) for files where the rotology is from genuinely-missing definitions + Strategy A (qualification) ONLY for the residual symbols after B+D have been applied.
- **Blast radius**: scaled correctly to rotology type per file.
- **Risk**: requires rotology-classification per file (audit upfront work).
- **Pro**: addresses both ROOT CAUSE (B) and SYMPTOM (A) appropriately; minimal over-qualification.
- **Con**: highest upfront complexity (audit + ADR + 3-path implementation).

## §Recommended ADR decision strategy

1. **Audit phase** (1 chore): enumerate rotology per file as (a) scope-lookup (Strategy B), (b) missing-include (Strategy D), (c) genuinely orphan symbol requiring qualification (Strategy A).
2. **ADR phase** (this chore's deliverable): write ADR with the audit results + chosen strategy + blast-radius commitment.
3. **Implementation phase** (3 chores, each area-grouped per AGENTS.md "Fare PR piccole e mirate"):
   - Chore B: apply Strategy B to scope-lookup rotology files
   - Chore D: apply Strategy D to missing-include rotology files
   - Chore A: apply Strategy A to residual orphan-symbol files
4. **Verification phase**: rebuild 4 OBJECT libs, smoke ctest, 11/11 baseline green, golden regen.

## §Forward-points

- Decision ratification (this ADR) — Cat-3 anti-dup canonical ticket-home.
- Implementation strategy definition (B + D + A combined, scoped by per-file audit).
- Blast-radius commitment (target: 27+ files modified, 150-200 LoC delta).
- Cross-validate against AGENTS.md "Feature freeze" + "Fare PR piccole e mirate" + §Docs canonical update discipline.

## §References

- [TICKET-SYSTEMIC-NAMESPACE-ROT](TICKET-SYSTEMIC-NAMESPACE-ROT.md) — predecessor ticket, CLOSED-WONTFIX 2026-07-21 with rotology blast-radius disclosure
- [TICKET-GRAPHICS-SHAPE-STYLE-ROT](TICKET-GRAPHICS-SHAPE-STYLE-ROT.md) — original 5-file rot ticket (DONE-PARZIALE per upstream commit)
- AGENTS.md §Feature freeze "no nuova API SDK" — namespace modifications touch `include/chronon3d/**/*.hpp` (public SDK surface); ADR-grade justification required
- AGENTS.md §Fare PR piccole e mirate — implementation MUST be split into 2-3 area-grouped chores per area (core/, render_graph/, registry/)
- AGENTS.md §Docs canonical update discipline rule — implementation chores MUST update canonical docs atomically (Cat-5 3-doc same-commit)
- C++ standard [C++23 §9.8.2 [namespace.namespace]] nested-namespace definition + GCC diagnostic formatter behavior for parent-scope lookup
- `/tmp/baseline_rot.log` — 394-error diagnostic captured at HEAD=323fe2ce (rotology evidence base)
- `/tmp/rot_qualify.py` — Op1 path-B reference implementation (closing `chronon3d_registry`, partial)
- `/tmp/rot_ns_convert.py` — Strategy B reference implementation (close-brace rebalancing, untested at scale)
