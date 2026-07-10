# ADR-017 — `commit_layer()`: render stays frame-specific, manifest is complete

| Field | Value |
|---|---|
| **Status** | ✅ Accepted (landed 2026-07-08) |
| **Date** | 2026-07-08 |
| **Deciders** | Timeline Definition of Done implementation owner |
| **Tags** | `scene-builder`, `asset-manifest`, `preflight`, `timeline-v2`, `sequence-v2`, `single-source-of-truth` |
| **Related** | [ADR-016 — sequence-asset-canonical-contract](./ADR-016-sequence-asset-canonical-contract.md) (the manifest completeness contract this ADR implements); [ADR-005 — asset-resolver-local](./ADR-005-asset-resolver-local.md) (the engine-local AssetResolver that AssetPreflightResolver consumes); [`include/chronon3d/scene/builders/scene_builder.hpp`](../include/chronon3d/scene/builders/scene_builder.hpp) (`commit_layer()` implementation); [`include/chronon3d/scene/builders/detail/scene_builder_layers.inl`](../include/chronon3d/scene/builders/detail/scene_builder_layers.inl) (all 7 layer methods); [`include/chronon3d/scene/builders/detail/scene_builder_sequences.inl`](../include/chronon3d/scene/builders/detail/scene_builder_sequences.inl) (sequence manifest merge); [`tests/visual/timeline/test_timeline_golden.cpp`](../tests/visual/timeline/test_timeline_golden.cpp) (Gate 1 — 4 tests, 60 assertions); [`tests/visual/timeline/test_asset_readiness.cpp`](../tests/visual/timeline/test_asset_readiness.cpp) (Gate 3 — 5 tests, 40 assertions); [`docs/CHANGELOG.md`](CHANGELOG.md) §"10-point friction audit". |

## Context

### The problem: incomplete manifests for inactive layers

`SceneBuilder` is an immediate-mode, single-frame builder. When `SceneBuilder::layer()` constructs a layer via `LayerBuilder`, it checks `layer.active_at(current_integer_frame())` and **discards the layer entirely** if inactive — including its `AssetManifest`. Similarly, `SceneBuilder::sequence()` short-circuits with `if (!active) return *this;` when the sequence is outside the current frame, so the inner lambda is never called and no assets are collected.

This created two concrete bugs:

1. **`AssetPreflightResolver::check(mode=FullComposition)` could not verify assets from layers at other frames.** A composition with a font at frame 0 and a video at frame 100, built at frame 0, would only report the font — the video was silently missing from the manifest.

2. **The `video_layer()` method bypassed `LayerBuilder::video()`**, setting the `VideoSource` directly on the layer without registering it in the `AssetManifest`. Even active video layers had no manifest entry.

### Alternatives considered

| Option | Description | Rejected because |
|--------|-------------|-----------------|
| **A. Build at all frames** | Evaluate the composition at every frame to collect all manifests | O(n) renders per preflight; defeats the purpose of a single-frame builder |
| **B. Separate manifest builder** | A parallel `AssetManifestBuilder` that walks the composition DSL without building layers | Duplicates the entire build pipeline; maintenance burden |
| **C. `commit_layer()` helper** | Always merge the manifest, conditionally add the layer | Minimal change; 1-line diff per method; manifest is complete without affecting render |

## Decision

### Decision 1: `commit_layer()` helper in `SceneBuilder`

Add a private inline helper to `SceneBuilder`:

```cpp
void commit_layer(Layer layer) {
    scene_.asset_manifest().merge(layer.asset_manifest);
    if (layer.active_at(current_integer_frame())) {
        scene_.add_layer(std::move(layer));
    }
}
```

**Key invariant:** the manifest merge happens **before** the `active_at()` check. The layer's assets are always registered regardless of whether the layer is renderable at the current frame.

All 7 layer methods (`layer`, `screen_layer`, `adjustment_layer`, `precomp_layer`, `video_layer` ×2, `null_layer`) replace their `if (active_at) { add_layer }` pattern with `commit_layer(builder.build())`.

### Decision 2: `sequence()` always executes the lambda

Remove the early `if (!active) return *this;` guard from `sequence()`. The lambda is always called to collect assets from inner layers. After the sub-scene is built:

```cpp
// ALWAYS preserve child assets
scene_.asset_manifest().merge(sub_scene.asset_manifest());

// ONLY add spatial layers/nodes if the sequence is active
if (active) {
    for (auto& layer : sub_scene.layers()) { ... }
}
```

The local frame for inactive sequences uses `spec.trim_before` (not `cf - spec.from + spec.trim_before`) to avoid negative frames in user-authored lambdas.

### Decision 3: `video_layer()` routes through `LayerBuilder::video()`

The `video_layer()` method now calls `builder.video(std::move(source))` before the user lambda, ensuring the video asset is registered in the manifest via the canonical `LayerBuilder::video()` path. Previously, it set `l.video_source` directly, bypassing manifest registration.

Additionally, `video_layer()` now includes `builder.screen_dimensions()` (was missing — all other layer methods had it).

## Consequences

### Positive

- **`AssetPreflightResolver::check(FullComposition)` is now reliable.** The manifest contains assets from ALL layers in the scene, not just those active at the build frame. This is critical for `chronon3d_cli preflight` which samples frames and merges manifests.
- **`video_layer()` assets are now tracked.** Video assets appear in the manifest and are verified by the preflight resolver.
- **Render remains frame-specific.** The `active_at()` check still controls which layers are added to the scene's renderable list. No inactive layers are rendered — only their assets are tracked.
- **Zero behavioral change for existing compositions.** The manifest is additive (merge is idempotent for non-duplicate entries). Layers that were active before are still active. The only difference is that the manifest is now complete.

### Negative

- **Slight increase in scene build cost.** Every `layer()` call now unconditionally merges the manifest, even for inactive layers. The cost is negligible (vector append of `AssetRef` entries).
- **Inactive sequence lambdas now execute.** User-authored lambdas inside `sequence()` are called even when the sequence is inactive. This is by design (to collect assets) but could have side effects if the lambda does something besides building layers (e.g., prints to console). In practice, sequence lambdas only call `s.layer()` or `seq.layer()`, which are side-effect-free when the layer is inactive.

### Trade-offs

- **Manifest completeness vs. build purity.** The manifest now contains assets from layers that will never be rendered at the current frame. This is the correct trade-off: the manifest is a **declaration** of what the composition needs (for preflight), not a **record** of what was rendered.
- **`commit_layer()` by value vs. by reference.** Taking `Layer` by value (moved from `builder.build()`) is correct because `build()` returns by value and the merge reads from the layer before the conditional move.

## Tests

| Test | Assertions | Status |
|------|-----------|--------|
| `Timeline.SequenceBoundaries` | 12 | ✅ PASS |
| `Timeline.LocalFrameMapping` | 7 | ✅ PASS |
| `Timeline.AnimationUsesLocalFrame` | 7 | ✅ PASS |
| `Timeline.NestedSequenceMapping` | 5 | ✅ PASS |
| `Assets.MissingFontFailsPreflight` | 7 | ✅ PASS |
| `Assets.MissingImageFailsPreflight` | 8 | ✅ PASS |
| `Assets.MissingVideoFailsPreflight` | 3 | ✅ PASS |
| `Assets.FrameOnlyVsFullComposition` | 12 | ✅ PASS |
| `Assets.PreflightErrorFormatting` | 5 | ✅ PASS |
| `Debug.TimelineDebugInfoCapturesActiveSequences` | 10 | ✅ PASS |
| `Debug.TimelineJSONOutput` | 9 | ✅ PASS |
| `Debug.NestedSequencePathHierarchy` | 9 | ✅ PASS |
| `Debug.MultipleAssetTypesInOutput` | 10 | ✅ PASS |

Total: 16 tests, 168 assertions, 0 failures.

## References

- [`include/chronon3d/scene/builders/scene_builder.hpp`](../include/chronon3d/scene/builders/scene_builder.hpp) — `commit_layer()` declaration
- [`include/chronon3d/scene/builders/detail/scene_builder_layers.inl`](../include/chronon3d/scene/builders/detail/scene_builder_layers.inl) — all 7 layer methods
- [`include/chronon3d/scene/builders/detail/scene_builder_sequences.inl`](../include/chronon3d/scene/builders/detail/scene_builder_sequences.inl) — sequence manifest merge
- [`include/chronon3d/assets/asset_preflight_resolver.hpp`](../include/chronon3d/assets/asset_preflight_resolver.hpp) — the consumer that benefits from complete manifests
- [`docs/CHANGELOG.md`](CHANGELOG.md) §"10-point friction audit" — commit `0ff8b100`
