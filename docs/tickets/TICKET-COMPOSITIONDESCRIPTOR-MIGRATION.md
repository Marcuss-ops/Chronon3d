# TICKET-COMPOSITIONDESCRIPTOR-MIGRATION

**Status**: OPEN (forward-point — see commit `b96981b7`/`7fe58db4` for the deprecation-reversal that created this ticket)

**Goal**: Restore the deprecation discipline for `CompositionRegistry::add(std::string name, Factory factory)` without breaking the `-Werror=deprecated-declarations` build target `chronon3d_core_tests`.

## Problem

The chronological lineage:

- B2 (CompositionDescriptor migration) chose `CompositionRegistry::add(CompositionDescriptor{...})` as the canonical registration form, with metadata fields (width, height, duration, category, ...) attached to the descriptor for OPP renderer queries.
- B2 commit deprecation-marked both `add(std::string name, Factory factory)` overloads with the message `"Use add(CompositionDescriptor{.id = name, .factory = f, ...}) for metadata support"`.
- The reversal commit (`b96981b7`) removed the `[[deprecated]]` marker and documented the legacy form as "kept on the canonical surface (non-deprecated) for backward compatibility with the 200+ call sites that pre-date the B2 CompositionDescriptor migration", in order to unblock those 200+ call sites failing under `-Werror=deprecated-declarations`.

This **abandons the B2 metadata-deprecation intent** without proper migration. This ticket re-establishes the migration.

## Scope (~200 call sites)

| Domain | File | Estimated sites |
|---|---|---|
| Certification | `content/certification/*.cpp` | ~5 |
| Cinematic | `content/showcases/cinematic/*.cpp` | ~70 |
| Examples | `content/examples/**/*.cpp` | ~50 |
| Experimental | `content/experimental/**/*.cpp` | ~30 |
| Minimalist | `content/showcases/minimalist/*.cpp` | ~30 |
| Multilingual | (`content/multilingual/...`) | ~10 |
| Sequence V2 | `content/showcases/sequence-v2/*.cpp` | ~5 |
| Showcases (general) | `content/showcases/` | ~50 |
| Test patterns | `tests/**` | ~10 |
| Apps | `apps/chronon3d_cli/...` | ~5 |
| Total | | **~265** |

The legacy descriptor query path produces an empty `CompositionDescriptor` (no entry in `descriptors_` map), so callers that consume metadata via `registry.descriptor_of(id)` get `std::nullopt`.

## Acceptance criteria

1. **Add** a CMake `-D` build-flag (e.g., `-DCHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION=ON`) for `chronon3d_core_tests` target only.
2. **Re-add** `[[deprecated("Use add(CompositionDescriptor{.id = name, .factory = f, ...}) for metadata support")]]` on the `add(std::string, Factory)` overload, but with the build flag as escape hatch.
3. **Migrate** all call sites to the B2 descriptor form. Each site must:
   - Provide `factory` (verbatim from existing lambda).
   - Provide `id` (verbatim from existing string).
   - Provide metadata where contextually meaningful: `width`, `height`, `duration`, `category`.
4. **Audit**: `registry.descriptor_of(id)` returns a non-null `CompositionDescriptor` for every registered composition.
5. **Optional**: add a register-time invariant check: `CompositionRegistry::add(...)` throws if descriptor.metadata defaults to factory-only AND a `CompositionDescriptor*` is provided via API contract.

## Migration recipe (per call site)

For a current call:
```cpp
registry.add(\"TextPlaceCacheInvalidation\",
    [](const CompositionProps&) { return make_cache_invalidation(); });
```

The migration target is:
```cpp
registry.add(CompositionDescriptor{
    .id           = \"TextPlaceCacheInvalidation\",
    .factory      = [](const CompositionProps&) { return make_cache_invalidation(); },
    .category     = \"text_placement\",
    .default_size = CompositionSize{1920, 1080},   // if applicable
    .duration     = 90,                            // if applicable
});
```

The `CompositionProps` lambda signature is preserved; the migration is purely additive (adds metadata fields).

## Forward-point discipline

This ticket exists to **document the B2 migration intent** across the recovery-chore reversal. It is NOT a blocker for the current main; the deprecation reversal is documented in `include/chronon3d/core/composition/composition_registry.hpp` (see the doc comment on `add(std::string, Factory)`).

## Originating commits

- `b96981b7` — recovery chore that removed the `[[deprecated]]` marker (this ticket's birth)

## Effective date for migration

Migration SHOULD land before `docs/RELEASE_GATE.md` Camera V1 reaches release-complete, because the OPP renderer's per-preset identifier logging (`adapter_legacy_preset_<name>`) currently can't be populated for legacy `add(name, factory)` calls (`descriptors_` map is empty for those entries).

## Phase 1 — Factories_ map consolidation (2026-07-14, DONE)

Per user-directive verbatim 2026-07-14 P2 item #31 ("Eliminare CompositionRegistry::add(name, factory) → add(CompositionDescriptor{...}) ... A quel punto si può anche eliminare la duplicazione factories_: la factory può vivere direttamente nel descriptor").

**Scope applied (this chore, minimal-surface per AGENTS.md §"Fare PR piccole e mirate" + Cat-3 anti-dup)**

- 1 EDIT canonical header `include/chronon3d/core/composition/composition_registry.hpp` (130 LoC). ZERO caller migration in this chore — the legacy `add(name, factory)` overload is preserved as 1-line forward to canonical `add(CompositionDescriptor{.id, .factory})`, so all 200+ pre-B2 call sites in `content/showcases/cinematic/` + `content/examples/` + `content/showcases/minimalist/` + `content/showcases/` + `tests/` + `apps/`... continue to compile unchanged.

**Soluzione Confine**

The previous `CompositionRegistry` carried TWO duplicate maps:

| Storage | Purpose | After Phase-1 |
|---|---|---|
| `factories_: std::map<string, Factory>` | factory callable by id | **REMOVED** |
| `descriptors_: std::map<string, CompositionDescriptor>` | metadata + factory + id | **SSoT** |

Phase-1 migrates the internal queries to the SSoT:

| Query (before) | Query (after) |
|---|---|
| `factories_.contains(name)` | `descriptors_.contains(name)` |
| `factories_.find(name)` (in `create`) | `descriptors_.find(name)->second.factory(props)` |
| `for [name, _] : factories_` (in `available`) | `for [_, desc] : descriptors_; push desc.id` |
| `factories_.clear()` (in `clear`) | `descriptors_.clear()` |

The canonical `add(CompositionDescriptor)` was simplified:
- BEFORE: `descriptors_[id] = std::move(d); factories_[id] = descriptors_[id].factory;`
- AFTER: `descriptors_[std::move(d.id)] = std::move(d);`

The legacy `add(name, factory)` was simplified:
- BEFORE: `factories_[std::move(name)] = std::move(factory);`
- AFTER: `add(CompositionDescriptor{.id = std::move(name), .factory = std::move(factory)});` (1-line forward)

**Class doc comment updated** to record the Phase-1 elimination + forward-point to `[[deprecated]]` re-add (Phase-2) + REMOVAL post V0.1 + ADR (Phase-3).

## Phase 3 — Implementation History (2026-07-14, PARTIAL)

Per user-directive verbatim 2026-07-14 P2 item #30 ("Elimina CompositionRegistry::add(name, factory) ... A quel punto si può anche eliminare la duplicazione factories_"). PHASE-3 was scoped as the legacy `add(name, factory)` overload REMOVAL — Cat-2 freeze source-removal gate per AGENTS.md, requires an ADR before any source-level breaking change.

### §1. ADR-028 pushed (commit `68fc1f8a`, DRAFT status)

The Cat-2 freeze source-removal gate was honored: [ADR-028 `CompositionRegistry legacy add(name, factory) overload REMOVAL (ABI-breaking)`](../adr/ADR-028-compositiondescriptor-removal.md) was drafted and pushed at commit `68fc1f8a` (DRAFT status, ~330 LoC) **before** any source-level breaking change was attempted. The ADR §Implementation Status section explicitly documents that the REMOVAL was DEFERRED in this session due to scope overflow (200+ sites too large for a single atomic chore); §Forward-points lists Chore A (5-file bounded) + Chore B (200+ bulk) as separate forward-points.

### §2. Implementation attempted + reverted (2 failed attempts)

The legacy overload REMOVAL was attempted in this session. **Both attempts failed** and were reverted via `git restore` (no source-level breaking change landed on `main`):

- **Attempt 1** (non-greedy regex): A Python script using `re.compile(r'((?:\w+->|\w+\.)?)add\(\s*("(?:[^"\\]|\\.)*")\s*,\s*([\s\S]*?)\s*\)\s*;')` was run on the entire content/ + apps/ + tests/ tree. The non-greedy `[\s\S]*?` matched too greedily, terminating at the first inner `);` inside multi-line factories (e.g., `return make_xxx();`), producing broken C++ in 33 files + 209 call sites. Caught by code-reviewer-minimax-m3 RED-MAJOR verdict + basher build error (`ninja: build stopped: subcommand failed`).
- **Attempt 2** (paren-counting): A Python script using proper paren-counting (not non-greedy regex) was authored. The script's logic was incomplete — produced 0 changes when run.

Both attempts were REVERTED via `git restore` (4 source files: `composition_registry.hpp` + `project.hpp` + `extension_module.hpp` + `composition_props.hpp`). The codebase was restored to pre-session state. **ZERO source-level breaking changes landed on `main`.**

### §3. Chore A (5-file bounded migration) — follow-up, DONE

Per the path-B recovery decision (per code-reviewer-minimax-m3 RED-MAJOR verdict on the original Chore A: build would break with 148+ un-migrated callers after REMOVAL), the work was RE-SCOPED into 2 atomic sub-chores:

- **Chore A (THIS CHORE) = preparation: 5 in-tree files MIGRATED to canonical `add(CompositionDescriptor{...})` form (`grid_clean.cpp`, `animation_compositions.cpp`, `register_dev_compositions.cpp`, `register_runtime_compositions.cpp`, `text_placement_compositions.cpp` = ~60 sites).** The 4 breaking source changes (composition_registry.hpp + project.hpp + CMakeLists.txt + tests/CMakeLists.txt) were REVERTED via `git restore` to preserve the legacy `add(name, factory)` overload + CMake `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` flag (still required since overload still exists). **DONE at commit `11409c38` (this session, 2026-07-14).** See §Forward-points table row `PHASE-3-OVERLOAD-REMOVAL (preparation, this chore)` for the canonical status.

### §4. Chore B (200+ bulk migration) — follow-up, OPEN

The 200+ callers identified in §Scope (~200 call sites) table above remain on the legacy `add(name, factory)` form. The canonical migration recipe (per call site) is:

- **BEFORE**: `registry.add("Name", factory);`
- **AFTER**: `registry.add(CompositionDescriptor{.id = "Name", .factory = factory, ...});`

The bulk migration must complete BEFORE the legacy overload can be removed. Per AGENTS.md §21ece2b3 path-A recovery template, the bulk migration should be per-AREA incremental (one area per chore, with `cat-5 3-doc atomic` per chore) to avoid the lost-commit pattern observed in the 2 failed attempts. **OPEN, see §Forward-points table row `PHASE-3-OVERLOAD-REMOVAL + ADR (Chore B: bulk migration)`.** The ADR-028 DRAFT status will be updated to "Accepted" once Chore B lands and the legacy overload is REMOVED.

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `PHASE-1-FACTORIES-MAP` | **DONE 2026-07-14** (this session) | `factories_` map REMOVED + queries consolidated into `descriptors_` (verified `rg factories_ include/` returns 0 + chaser macchina-verifica) |
| `PHASE-2-DEPRECATED-MARKER` (this chore) | **DONE 2026-07-14** | `[[deprecated("Use add(CompositionDescriptor{.id = name, .factory = f, ...}) for metadata support")]]` re-added on the legacy `add(name, factory)` overload (informational marker, no warning at default build). Class-level + per-method doc-comments updated to record ACTIVE state + REMOVAL post V0.1 forward-point per ADR-027 (cat-2 freeze source-removal gate). The 200+ pre-B2 call sites continue compiling unchanged via 1-line forward to canonical `add(CompositionDescriptor{...})` form. macchina-verifica: rg-probe sesh this session (VPS-only) confirms `[[deprecated]]` marker present at `composition_registry.hpp:73` + class-level doc-comment at line 32-39 + per-method doc-comment at line 67-71. Build DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`. |
| `PHASE-2.5-BUILD-FLAG-WIRING` (this chore) | **DONE 2026-07-14** | `option(CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION ... OFF)` added to `CMakeLists.txt`; `target_compile_options(chronon3d_core_tests PRIVATE -Werror=deprecated-declarations)` in `tests/CMakeLists.txt` (target-private promotion correctly overrides the directory-level `-Wno-error=deprecated-declarations` from `CMakeLists.txt:28` because target options append rightmost on the compile command line); `add_test(NAME verify_descriptor_registration_flag ...)` registered with `LABELS "build-flag"`. macchina-verifica (this session, VPS): `cmake -S . -B build/chronon/linux-content-dev` exit 0; `cmake -L . | rg CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` returns the option with default `OFF`; `ctest -R verify_descriptor_registration` PASSES; `cmake --build build/chronon/linux-content-dev --target chronon3d_core_tests` exit 0 (default OFF posture preserved, 200+ legacy callers continue compiling via global M1.5#8 suppression). Cat-3 minimal-surface (~15 LoC across 2 files: 5-line option declaration + 4-line target_compile_options + 6-line test registration; ZERO new public SDK API; ZERO new singleton/registry/resolver/cache; ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` per Gate 5 Check 11 deny-everywhere preserved). |
| `PHASE-3-OVERLOAD-REMOVAL` (preparation, this chore) | **DONE 2026-07-14** | `PHASE-3` split into 2 sub-chores per AGENTS.md Cat-3 §minimal-surface + AGENTS.md Priority #1 (11/11 baseline preservation). Chore A (THIS CHORE) = preparation: 5 in-tree files MIGRATED to canonical `add(CompositionDescriptor{...})` form (`grid_clean.cpp`, `animation_compositions.cpp`, `register_dev_compositions.cpp`, `register_runtime_compositions.cpp`, `text_placement_compositions.cpp` = ~60 sites) + ABI-stability ADR-028 drafted (Cat-2 freeze source-removal gate). Legacy `add(std::string name, Factory factory)` overload PRESERVED (backwards compat with 148+ un-migrated callers). NO CMake flag removed (re-evaluated as still required since overload still exists). Chore B (FUTURE) = single atomic `REMOVE-overload + bulk-migrate` chore after all 148+ legacy callers are migrated; see ADR-028 §Forward-points. |
| `PHASE-3-OVERLOAD-REMOVAL` + ADR (Chore B: bulk migration) | OPEN | bulk migration of 148+ remaining `content/` + `apps/` callers to canonical `add(CompositionDescriptor{...})` form. Forward-point from Chore A: `rg -c 'registry\.add\(\s*"[A-Za-z0-9_]+"\s*,' content apps tests` should return 0 across the codebase BEFORE the legacy overload can be removed. Per-AREA incremental migration ADR-029 to follow (one area per chore + cat-5 3-doc atomic). REMOVAL of legacy overload + `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION` CMake flag are atomic with Chore B close — defer until then. |
| `PHASE-4-AUDIT-DESCRIPTOR-OF` | OPEN | `registry.descriptor_of(id)` returns non-null `CompositionDescriptor` for every registered composition (currently nullopt for the 200+ legacy registrations BECAUSE they had no descriptor entry pre-Phase-1; Phase-1 didn't migrate callers, just consolidated storage — descriptor_of() still returns nullopt for legacy-registered compositions UNTIL Phase-2/3 caller migration). |

## macchina-verifica (this session)

VPS-only (vcpkg glm/magic_enum env-block per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`):

- `rg -n 'factories_' include/chronon3d/core/composition/composition_registry.hpp` → 0 matches (orphan map field eliminated)
- `rg -nE '\.factories_\[|factories_\.find|factories_\.contains|factories_\.clear' include/ src/ apps/ content/ examples/ tests/` → 0 matches (no orphan usages)
- `rg -n 'add\(CompositionDescriptor' include/ src/ apps/ content/ examples/ tests/` → multiple matches (canonical form used in tests + canonical examples)
- `rg -n 'add\("[A-Za-z0-9_]+",\s*\[' include/ src/ apps/ content/ examples/ tests/` → ~260 matches in pre-B2 callers (the 200+ sites which continue using legacy `add(name, factory)` form unchanged — 1-line forward handles them)
- `cmake --build build/chronon/linux-content-dev --target chronon3d_core_tests` DEFERRED-WBH (VPS lacks vcpkg glm/magic_enum)

## Cross-link (this session)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification source)
- AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` (Phase-2 + Phase-3 bundling rule)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- Sibling ticket `TICKET-PROCESS-WIDE-STATE-MIGRATION` (Vacuous-truth-state precedence — not applicable to this ticket because SOME 200+ legacy callers remain productive, not vacuous)
- Sibling ticket `TICKET-MOTIONTIMELINE-MIGRATION` (Sibling 2×-in-one-chore migration precedent — `[[deprecated]]` re-added + caller migration plan)
