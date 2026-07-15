# TICKET-COMPOSITIONDESCRIPTOR-MIGRATION-CHORE-B-BULK-MIGRATION

**Status**: IN PROGRESS

## Goal

Migrate every bundled `CompositionRegistry::add(name, factory)` caller to
`CompositionDescriptor` registration, one area at a time. Remove the legacy
overload and its CMake enforcement flag only after the repository-wide caller
count reaches zero.

Parent references:

- [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md)
- [ADR-028-compositiondescriptor-removal](../adr/ADR-028-compositiondescriptor-removal.md)

## Canonical registration

```cpp
registry.add(CompositionDescriptor{
    .id = "ProductLaunch",
    .category = "Marketing",
    .width = 1920,
    .height = 1080,
    .fps = FrameRate{30, 1},
    .duration = Frame{300},
    .factory = [](const CompositionProps&) { return product_launch(); }
});
```

Area-specific helpers may fill shared metadata, but every registration must
still enter the registry through `CompositionDescriptor`. No second registry or
factory store is permitted.

## Prepared composition contract

The canonical descriptor preparation phase now returns `PreparedComposition`:

```cpp
struct PreparedComposition {
    std::optional<CompositionMetadata> metadata;
    std::optional<assets::AssetManifest> asset_manifest;
    std::optional<std::filesystem::path> assets_root;
    std::function<Composition()> construct;
};
```

Typed props are decoded and validated once. The registry invokes the prepared
construction closure rather than decoding a second time. `chronon validate`
uses the declared manifest when available and only evaluates composition frames
for descriptors that have not yet declared their assets.

## Completed work

### Chore A — initial bounded migration

Commit `11409c38` migrated approximately 60 registrations in:

- `content/backgrounds/grid_clean.cpp`
- `content/animation_compositions.cpp`
- `apps/chronon3d_cli/register_dev_compositions.cpp`
- `apps/chronon3d_cli/register_runtime_compositions.cpp`
- `content/text_placement/text_placement_compositions.cpp`

### Certification area — DONE 2026-07-15

All eight source files registered by the certification section of
`content/CMakeLists.txt` now use the shared
`content/certification/certification_descriptor.hpp` helper and canonical
`CompositionDescriptor` registration:

- `cert_title.cpp`
- `cert_lower_third.cpp`
- `cert_long_text.cpp`
- `cert_multilingual.cpp`
- `cert_render_runtime.cpp`
- `cert_timeline.cpp`
- `cert_compositing.cpp`
- `cert_determinism.cpp`

This migrated 30 registrations and populated category, dimensions, FPS and
duration metadata. `cert_multilingual.cpp` and `cert_compositing.cpp` were also
collapsed onto shared data/build helpers to remove repeated registration and
scene-construction boilerplate.

### Sequence V2 area — DONE 2026-07-15

`content/showcases/sequence-v2/sequence_v2_compositions.cpp` now registers all
five compositions through canonical descriptors:

- `SeqV2IntroOutro`
- `SeqV2DeepNesting`
- `SeqV2StaggeredTimeline`
- `SeqV2TrimOffset`
- `SeqV2MixedMedia`

The descriptors publish dimensions, FPS, duration and category. Their prepared
phase declares the required Poppins font; `SeqV2MixedMedia` additionally
declares its optional placeholder image. The old `video` and `still` command
examples were replaced by the canonical `chronon render` surface, and the image
uses `ImageParams::asset_path` rather than the deprecated `path` member.

## Remaining areas

| Order | Area | Status |
|---:|---|---|
| 1 | `content/multilingual/` | Re-audit: no longer present in current content target |
| 2 | `content/certification/` | **DONE** |
| 3 | `content/showcases/sequence-v2/` | **DONE** |
| 4 | `content/experimental/` | OPEN |
| 5 | `content/showcases/minimalist/` | OPEN |
| 6 | `content/examples/` | OPEN |
| 7 | `content/showcases/` general | OPEN |
| 8 | `content/showcases/cinematic/` | OPEN |
| 9 | `tests/` | OPEN |
| 10 | `apps/chronon3d_cli/` | OPEN |

## Terminal removal gate

Before deleting the overload:

```bash
rg -n 'registry\.add\(\s*"' content apps tests
```

must return no source callers. Then, in one atomic cleanup:

1. remove `CompositionRegistry::add(std::string, Factory)`;
2. remove `CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION`;
3. remove the corresponding CMake verification test;
4. mark ADR-028 accepted;
5. close the parent migration ticket.

## Verification state

The certification and Sequence V2 source inventories are descriptor-only by
direct file review. Unit tests were updated to lock single-decode prepared
construction and declarative manifest resolution. A repository build and
`ctest` execution are still required on a machine with a working checkout and
dependencies; no green build is claimed by this update.
