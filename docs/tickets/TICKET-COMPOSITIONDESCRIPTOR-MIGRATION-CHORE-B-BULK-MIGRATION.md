# TICKET-COMPOSITIONDESCRIPTOR-MIGRATION-CHORE-B-BULK-MIGRATION

**Status**: OPEN (forward-point — Chore A preparation DONE at commit `11409c38` + ADR-028 DRAFT pushed at `68fc1f8a`; this Chore B is the second atomic sub-chore per AGENTS.md Cat-3 §minimal-surface + AGENTS.md Priority #1 11/11 baseline preservation)

**Goal**: Migrate the remaining 200+ legacy `CompositionRegistry::add(std::string name, Factory factory)` callers to canonical `add(CompositionDescriptor{.id, .factory, .category, .width, .height, .duration, .fps})` form, **one AREA per chore**, so that Chore A's legacy overload REMOVAL + `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` CMake flag removal can land as a single atomic chore after this ticket closes.

**Cross-link (parent)**:
- [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (parent ticket — Phase-1 + Phase-2 + Phase-2.5 + Phase-3-Chore-A already DONE; this ticket is the per-AREA incremental Chore B).
- [ADR-028-compositiondescriptor-removal.md](../adr/ADR-028-compositiondescriptor-removal.md) (DRAFT, source-removal design for the future Chore A overload REMOVAL + CMake flag removal; this Chore B is the prerequisite workload).
- [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION §Phase-3 Implementation History §4](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md#phase-3--implementation-history-2026-07-14-partial) (parent ticket's §Phase-3 Implementation History §4 documents Chore B as the deferred follow-up).

## Problem

Per [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION §Scope (~200 call sites)](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md#scope-200-call-sites), there are ~265 pre-B2 callers still using the legacy `registry.add("Name", factory)` form. Of those:
- 60 sites in 5 files (grid_clean.cpp + animation_compositions.cpp + register_dev_compositions.cpp + register_runtime_compositions.cpp + text_placement_compositions.cpp) were MIGRATED in [Chore A](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md#forward-points) (see Status line above for the canonical commit SHA, per AGENTS.md "SHA cite pattern (inline-only rule)").
- **200+ sites in 200+ files** remain on the legacy form. These are in:
  - `content/certification/*.cpp` (~5)
  - `content/showcases/cinematic/*.cpp` (~70)
  - `content/examples/**/*.cpp` (~50)
  - `content/experimental/**/*.cpp` (~30)
  - `content/showcases/minimalist/*.cpp` (~30)
  - `content/multilingual/...` (~10)
  - `content/showcases/sequence-v2/*.cpp` (~5)
  - `content/showcases/` (general, ~50)
  - `tests/**` (~10)
  - `apps/chronon3d_cli/...` (~5)

Per AGENTS.md Cat-2 freeze source-removal rule + the prior code-reviewer-minimax-m3 RED-MAJOR verdict on the original Chore A (200+ un-migrated callers would break the build after REMOVAL), the legacy `add(name, factory)` overload is PRESERVED for backwards compat. The CMake `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` build-flag is PRESERVED for the same reason. Both can only be REMOVED in a single atomic chore AFTER this Chore B ticket closes (200+ callers migrated to canonical form).

## Acceptance criteria

1. **Migrate all 200+ legacy callers** to canonical `add(CompositionDescriptor{...})` form. Each site must:
   - Provide `factory` (verbatim from existing lambda).
   - Provide `id` (verbatim from existing string).
   - Provide metadata where contextually meaningful: `category`, `width`, `height`, `duration`, `fps`.

2. **Per-AREA incremental strategy** (one area per chore + `cat-5 3-doc atomic` per chore):
   - One chore = one area from the §Scope table.
   - Each chore = (a) bulk migration of N sites in the area, (b) rebuild + ctest for the affected area, (c) commit + push + SHA-triple verify.
   - Per-chore ticket-home (e.g., `docs/tickets/TICKET-CHORE-B-AREA-CINEMATIC.md`) for cronaca estesa per AGENTS.md "Docs canonical update discipline rule" Cat-3 anti-dup.

3. **macchina-verifica** (terminal gate before this ticket can close):
   ```bash
   rg -c 'registry\.add\(\s*"[A-Za-z0-9_]+"\s*,' content apps tests
   ```
   MUST return 0 across the entire codebase.

4. **Final atomic chore** (after this ticket closes): REMOVE the legacy `add(name, factory)` overload + REMOVE the `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` CMake flag + REMOVE the `verify_descriptor_registration_flag` test in a single atomic chore. Update ADR-028 from DRAFT to "Accepted" + cat-5 3-doc atomic close of the parent ticket.

## Migration recipe (per call site)

For a current call:
```cpp
registry.add("TextPlaceCacheInvalidation",
    [](const CompositionProps&) { return make_cache_invalidation(); });
```

The migration target is:
```cpp
registry.add(CompositionDescriptor{
    .id           = "TextPlaceCacheInvalidation",
    .factory      = [](const CompositionProps&) { return make_cache_invalidation(); },
    .category     = "text_placement",
    .default_size = CompositionSize{1920, 1080},   // if applicable
    .duration     = 90,                            // if applicable
});
```

The `CompositionProps` lambda signature is preserved; the migration is purely additive (adds metadata fields). Per the parent ticket's `## Migration recipe (per call site)` section, the same recipe applies.

## Per-AREA strategy (proposed ordering by risk)

Per the AGENTS.md path-A recovery template (the same template that was applied to the 2 failed Chore A attempts; see Cross-link section above for the canonical reference, per AGENTS.md "SHA cite pattern (inline-only rule)"), the per-AREA incremental migration must be done carefully to avoid the lost-commit pattern. Proposed ordering (lowest-risk first):

| Order | Area | Estimated sites | Risk profile |
|---|---|---|---|
| 1 | `content/multilingual/` | ~10 | Low (small, isolated) |
| 2 | `content/certification/` | ~5 | Low (small, isolated) |
| 3 | `content/showcases/sequence-v2/` | ~5 | Low (small, isolated) |
| 4 | `content/experimental/` | ~30 | Medium (feature-gated) |
| 5 | `content/showcases/minimalist/` | ~30 | Medium (visually testable) |
| 6 | `content/examples/` | ~50 | Medium (large but non-prod) |
| 7 | `content/showcases/` (general) | ~50 | High (production showcases) |
| 8 | `content/showcases/cinematic/` | ~70 | High (largest, OPP-renderer-relevant) |
| 9 | `tests/**` | ~10 | Medium (test infrastructure) |
| 10 | `apps/chronon3d_cli/...` | ~5 | Medium (CLI dependencies) |

Each area = one chore = one commit = one ticket-home. The full Chore B sequence is **~10 atomic sub-chores**, each independently tested + pushed.

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `ADR-029-PER-AREA-MIGRATION-DESIGN` | OPEN | ADR-029 documents the per-AREA incremental migration strategy in detail (one area per chore, with `cat-5 3-doc atomic` per chore). Forward-point from this ticket; the ADR is the canonical design doc for the per-AREA strategy. |
| `CHORE-B-AREA-MULTILINGUAL` (~10 sites) | OPEN | `content/multilingual/` callers MIGRATED to canonical form. cat-5 3-doc atomic: per-area ticket-home + FOLLOWUP_TICKETS row + CHANGELOG cite-only. |
| `CHORE-B-AREA-CERTIFICATION` (~5 sites) | OPEN | `content/certification/` callers MIGRATED. |
| `CHORE-B-AREA-SEQUENCE-V2` (~5 sites) | OPEN | `content/showcases/sequence-v2/` callers MIGRATED. |
| `CHORE-B-AREA-EXPERIMENTAL` (~30 sites) | OPEN | `content/experimental/` callers MIGRATED. |
| `CHORE-B-AREA-MINIMALIST` (~30 sites) | OPEN | `content/showcases/minimalist/` callers MIGRATED. |
| `CHORE-B-AREA-EXAMPLES` (~50 sites) | OPEN | `content/examples/` callers MIGRATED. |
| `CHORE-B-AREA-SHOWCASES-GENERAL` (~50 sites) | OPEN | `content/showcases/` (general) callers MIGRATED. |
| `CHORE-B-AREA-CINEMATIC` (~70 sites) | OPEN | `content/showcases/cinematic/` callers MIGRATED (HIGHEST RISK, do last). |
| `CHORE-B-AREA-TESTS` (~10 sites) | OPEN | `tests/**` callers MIGRATED. |
| `CHORE-B-AREA-APPS` (~5 sites) | OPEN | `apps/chronon3d_cli/...` callers MIGRATED. |
| `CHORE-B-FINAL-OVERLOAD-REMOVAL` (single atomic after all 10 areas done) | OPEN | Legacy `add(name, factory)` overload REMOVED + `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` CMake flag REMOVED + `verify_descriptor_registration_flag` test REMOVED in one atomic chore. ADR-028 DRAFT → "Accepted". cat-5 3-doc atomic close of [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md). |

## macchina-verifica

```bash
# Per-AREA intermediate verify (after each chore):
rg -c 'registry\.add\(\s*"[A-Za-z0-9_]+"\s*,' content/<area>/
# Expected: 0 matches in the migrated area

# Per-chore ticket close verify:
rg -c 'registry\.add\(\s*"[A-Za-z0-9_]+"\s*,' content apps tests
# Expected: 0 matches across the entire codebase BEFORE the final atomic chore

# Final atomic chore verify (after this ticket closes):
rg -c 'add\(std::string name, Factory factory\)' include/chronon3d/core/composition/composition_registry.hpp
# Expected: 0 matches (legacy overload REMOVED)
```

## macchina-verifica (current state, 2026-07-14)

Per the parent ticket's `## macchina-verifica (this session)` + the FOLLOWUP_TICKETS row (status: PARTIAL 2026-07-14), the current rg-probe returns ~260 matches across the codebase. This is the **Chore B debt** that this ticket exists to retire.

## Cross-link

- [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (parent ticket — §Phase-3 Implementation History §4 documents Chore B as the deferred follow-up)
- [ADR-028-compositiondescriptor-removal.md](../adr/ADR-028-compositiondescriptor-removal.md) (DRAFT — source-removal design for the future Chore A overload REMOVAL; ADR-029 will be the per-AREA migration design)
- [AGENTS.md §21ece2b3 path-A recovery template](../../../AGENTS.md) (the canonical recovery template for per-AREA incremental migration; same template applied to the 2 failed Chore A attempts)
- [AGENTS.md §Docs canonical update discipline rule](../../../AGENTS.md) (per-chore ticket-home is the canonical cronaca location; canonicals (CURRENT_STATUS/FOLLOWUP/CHANGELOG) only carry riga sintetica)
- [AGENTS.md Priority #1: 11/11 baseline preservation](../../../AGENTS.md) (per-AREA incremental migration preserves baseline; the lost-commit pattern from the 2 failed Chore A attempts is avoided by the per-AREA + cat-5 3-doc atomic discipline)
