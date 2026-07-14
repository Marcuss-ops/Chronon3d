# ADR-028 - CompositionRegistry legacy add(name, factory) overload REMOVAL (ABI-breaking)

## Status
**DRAFT** (2026-07-14, NOT implemented yet — see §Implementation Status below).

## Date
2026-07-14 (initial draft; implementation deferred to follow-up chores)

## Implementation Status (2026-07-14, DEFERRED)

This ADR was attempted in the same session it was drafted, but the implementation was REVERTED due to scope overflow:

- **Attempted**: 33 files + 209 call sites migrated to canonical form via a Python regex script
- **Result**: The first attempt used non-greedy regex which produced broken C++ (the `[\s\S]*?` non-greedy match terminated at the first inner `);` inside multi-line factories like `return make_xxx();`, producing syntax errors). The second attempt used paren-counting but the script logic was incomplete (0 changes produced).
- **Side effects of the first broken attempt**: 2 unintentional changes to `include/chronon3d/extension/extension_module.hpp` and `include/chronon3d/timeline/composition_props.hpp` (these were touched by the broken regex but not part of the planned scope).
- **Revert**: All 4 code changes (2 intentional + 2 unintentional) were reverted via `git restore`. The codebase is back to its pre-session state.
- **ADR-028 is preserved** (untracked, not committed) as the canonical design document for the future migration.

The 5-file bounded migration + 200+ bulk migration + 4 header changes + CMake flag removal are all DEFERRED to follow-up chores. See §Forward-points for the per-chore plan.

## Deciders
Agent3 + user-directive verbatim 2026-07-14 P2 item #30 ("Elimina CompositionRegistry::add(name, factory) ... A quel punto si può anche eliminare la duplicazione factories_").

## Tags
abi-break, public-sdk, cat-2-freeze, composition-descriptor, AGENTS.md, MIGRATION, b2, post-v0.1.

## Context
The chronological lineage of the composition-registration API:

1. **B2 (CompositionDescriptor migration)**: Chose `CompositionRegistry::add(CompositionDescriptor{...})` as the canonical registration form, with metadata fields (width, height, duration, fps, category) attached to the descriptor for OPP renderer queries. Marked the legacy `add(std::string name, Factory factory)` overload with `[[deprecated]]`.
2. **Reversal commit `b96981b7`**: Removed the `[[deprecated]]` marker to unblock 200+ pre-B2 callers failing under `-Werror=deprecated-declarations`. Documented the legacy form as "kept on the canonical surface (non-deprecated) for backward compatibility with the 200+ call sites that pre-date the B2 CompositionDescriptor migration".
3. **TICKET-COMPOSITIONDESCRIPTOR-MIGRATION Phase-1 (2026-07-14, DONE)**: `factories_` map REMOVED + queries consolidated to `descriptors_` SSoT. Legacy `add(name, factory)` preserved as 1-line forward to canonical `add(CompositionDescriptor{.id, .factory})`.
4. **Phase-2 (2026-07-14, DONE)**: `[[deprecated]]` marker RE-ADDED + class doc-comment updated.
5. **Phase-2.5 (2026-07-14, DONE)**: `-DCHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION=OFF` build flag wired (escape hatch for `chronon3d_core_tests`).
6. **Phase-3 (THIS CHORE, 2026-07-14)**: REMOVE the legacy overload entirely.

The legacy overload is in the public SDK install surface (`include/chronon3d/core/composition/composition_registry.hpp:67-75`). Removing it is an ABI-breaking change. All in-tree callers must be migrated to the canonical form `add(CompositionDescriptor{.id, .factory, .category, .width, .height, .fps, .duration})`.

### Why this ADR is needed
Per AGENTS.md Cat-2 freeze ("No espansione API non necessaria + No nuovi singleton/registry/resolver/cache senza ADR" + "ABI-stability ADR required for source-removal in public SDK surface"), removing a public API symbol from `include/chronon3d/` requires an ADR. The legacy `CompositionRegistry::add(std::string name, Factory factory)` overload has been in the public SDK surface since pre-B2, with 200+ pre-B2 in-tree callers. ADR-028 closes the gap and provides the canonical place for future composition-registration-API decisions.

## Decision

Adopt the REMOVAL of the legacy `CompositionRegistry::add(std::string name, Factory factory)` overload + REMOVAL of `Project::add(std::string name, Factory factory)` (1-line forward at `project.hpp:106`):

1. **REMOVE** `CompositionRegistry::add(std::string name, Factory factory)` from `include/chronon3d/core/composition/composition_registry.hpp`. The canonical form is the ONLY entry point.
2. **REMOVE** `Project::add(std::string name, CompositionRegistry::Factory factory)` from `include/chronon3d/internal/project.hpp`. The canonical form is `Project::composition(name, spec, scene_fn)` (which internally calls canonical `add(CompositionDescriptor)`).
3. **UPDATE** `Project::composition()` internal call to use the canonical form: `m_registry.add(CompositionDescriptor{.id = ..., .factory = ..., .width = ..., .height = ..., .fps = ..., .duration = ...})` with metadata derived from the `CompositionRegistrationSpec spec` parameter.
4. **REMOVE** the `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` CMake build flag (now moot — the overload is removed, the deprecation is unconditional). The flag, the target_compile_options line, and the test `verify_descriptor_registration_flag` are all REMOVED.
5. **MIGRATE** all in-build-path callers to the canonical form. The 5 known in-tree files (in the build path) are migrated in this chore:
   - `content/backgrounds/grid_clean.cpp` (1 site)
   - `content/animation_compositions.cpp` (15 sites)
   - `apps/chronon3d_cli/register_dev_compositions.cpp` (15+ sites)
   - `apps/chronon3d_cli/register_runtime_compositions.cpp` (3 sites)
   - `content/text_placement/text_placement_compositions.cpp` (25+ sites)
6. **DOCUMENT** the 200+ remaining content/ sites (in `content/showcases/cinematic/`, `content/examples/`, `content/experimental/`, `content/showcases/minimalist/`, `content/multilingual/`, `content/showcases/sequence-v2/`, `content/showcases/`, and `tests/` patterns) as forward-points in `docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md` for incremental migration in follow-up chores. These sites are NOT in the build path of `chronon3d_core_tests` (the strictest target), so the core build remains green.

### Migration recipe (canonical form)

For each legacy call:
```cpp
registry.add("Name", [](const CompositionProps&) { return make_xxx(); });
```

The migration target is:
```cpp
registry.add(CompositionDescriptor{
    .id = "Name",
    .factory = [](const CompositionProps&) { return make_xxx(); },
    // optional: .category, .width, .height, .fps, .duration
});
```

The factory body is preserved verbatim; the migration is purely additive (adds metadata fields + the `CompositionDescriptor{...}` wrapper).

## Consequences

### Positive
- Single canonical registration form: `add(CompositionDescriptor{...})`. No ambiguity.
- Eliminates the deprecation-marker loophole (the `[[deprecated]]` macro is removed entirely; no more -Wno-error=deprecated-declarations suppression needed for the composition API).
- `descriptors_` map is the SSoT for both metadata queries AND factory storage (Phase-1 already established this).
- `registry.descriptor_of(id)` returns a non-null `CompositionDescriptor` for every registered composition (after caller migration in follow-up chores).
- Build flag `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` is REMOVED (cleanup of dead configuration).

### Negative
- **ABI-breaking change**: any external SDK consumer using `registry.add(name, factory)` must migrate. The migration is straightforward (mechanical pattern substitution).
- The 200+ pre-B2 content/ sites in `content/showcases/cinematic/`, `content/examples/`, etc. are NOT migrated in this chore. They are documented as forward-points in TICKET-COMPOSITIONDESCRIPTOR-MIGRATION. The `chronon3d_content` target build will FAIL until these sites are migrated (the `chronon3d_core_tests` target remains green because the migrated 5 files cover all in-build-path callers).
- The 5-file migration is bounded; the bulk migration is a follow-up chore.

### Neutral
- The deprecation reversal commit (cited in §Context) is documented as a historical event. The original B2 deprecation intent is restored post-Phase-3.
- `Project::composition()` signature is UNCHANGED (the internal canonical-form call is transparent to callers).
- The 11/11 baseline gate suite is NOT affected (no behavioral change to existing compositions).

## Alternatives Considered

### A1. Flag-gated removal (CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION=ON)
Rejected: The user explicitly said "REMOVE the legacy add(name, factory) overload entirely". A flag-gated approach leaves the overload in the default OFF posture, which is a non-removal. ADR-028 adopts unconditional removal.

### A2. Keep the 1-line forward + just remove the `[[deprecated]]` marker (status quo)
Rejected: This is the current state (Phase-2). It abandons the B2 metadata-deprecation intent. The user wants the legacy surface ELIMINATED, not just un-deprecated.

### A3. Bulk-migrate all 200+ content/ sites in this chore
Rejected: The 200+ sites are in files that are NOT in the build path of `chronon3d_core_tests` (the strictest target). The core build remains green with the bounded 5-file migration. The bulk migration is a follow-up chore to keep this PR scope-bounded per AGENTS.md §"Fare PR piccole e mirate".

### A4. Keep `Project::add(name, factory)` as a 1-line forward to the canonical form
Rejected: Inconsistent with the registry removal (the registry's legacy overload is removed, but `Project::add` would still exist as a legacy entry point). Cat-3 anti-duplication rule. REMOVED for consistency.

## Migration scope (this chore, 5 files)

| File | Sites | Pattern |
|---|---|---|
| `content/backgrounds/grid_clean.cpp` | 1 | `registry.add("Name", lambda)` |
| `content/animation_compositions.cpp` | 15 | `registry.add("Name", lambda)` (mix of `#ifdef` + plain) |
| `apps/chronon3d_cli/register_dev_compositions.cpp` | 15+ | `registry.add("Name", named_func_call)` (all in `void register_dev_compositions()`) |
| `apps/chronon3d_cli/register_runtime_compositions.cpp` | 3 | `registry.add("Name", make_landscape_comp)` (shared lambda + portrait) |
| `content/text_placement/text_placement_compositions.cpp` | 25+ | `registry.add("Name", lambda)` |

Total: ~60 sites migrated in this chore.

## Forward-points (out of scope, follow-up chores)

- 200+ sites in `content/showcases/cinematic/`, `content/examples/`, `content/experimental/`, `content/showcases/minimalist/`, `content/multilingual/`, `content/showcases/sequence-v2/`, `content/showcases/`, `tests/` patterns. Each follow-up chore migrates 1 file or a small group of files.
- `chronon3d_content` target build will FAIL until the content/ sites are migrated. The `chronon3d_core_tests` target remains green.

## References

- AGENTS.md Cat-2 freeze ("No espansione API non necessaria" + ABI-stability ADR gate).
- `docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md` — the parent ticket with full lineage (Phase-1/2/2.5/3) + forward-points.
- `docs/adr/ADR-022-divergence-window-gate.md` — ADR format precedent.
- `docs/adr/ADR-027-compositiondescriptor-migration.md` — the prior deprecation ADR.
- B2 (CompositionDescriptor migration) commits + reversal commit (cited in §Context inline).
- `include/chronon3d/core/composition/composition_registry.hpp` (the legacy overload source).
- `include/chronon3d/internal/project.hpp` (the `Project::add` + `Project::composition` source).
- `include/chronon3d/timeline/composition_descriptor.hpp` (the canonical `CompositionDescriptor` struct).
- `CMakeLists.txt` (the `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` flag — REMOVED in this chore).
