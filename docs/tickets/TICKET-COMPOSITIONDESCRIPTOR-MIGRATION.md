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
