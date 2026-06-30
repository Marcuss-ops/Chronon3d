#pragma once

// ============================================================================
// include/chronon3d/timeline/compiled_composition.hpp
//
// P3-C (V0.2 timeline staging) — `CompiledComposition` lives NEXT to
// the legacy `chronon3d::Composition` class.  It is the V2 staging
// handle for a ready-to-evaluate composition: the static recipe
// (`CompositionDefinition`) PLUS its compiled `camera_v1::CameraProgram`,
// keyed by a stable `fingerprint`.
//
// Why `shared_ptr<const ...>` for both members:
//   1. The composition recipe + the compiled camera program are
//      immutable once compiled; `const` enforces the "no-mutation-after-
//      compilation" contract at the type level.
//   2. `shared_ptr` lets a single CompiledComposition be retained by
//      multiple per-thread `RenderJob` and per-worker `CameraSession`
//      lifetimes without duplicating the heavy camera program state.
//
// Anti-DRY note (Rule 4 ANTI_DUPLICATION_RULES):
//   `CompiledComposition` is the V2 staging synonym for the legacy
//   `Composition`+`Composition::default_camera_descriptor(...)` pair.
//   The legacy surface stays put until V2 promotion closes.
// ============================================================================

#include <cstdint>
#include <memory>

#include <chronon3d/timeline/composition_definition.hpp>          // CompositionDefinition
#include <chronon3d/scene/camera/camera_v1/camera_program.hpp>   // camera_v1::CameraProgram

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompiledComposition
//
//   V2 staging struct.  Sits NEXT to (does NOT replace) the legacy
//   `chronon3d::Composition`.  Holds the immutable evaluate-ready handle:
//
//   * `definition`      \u2014 shared view of the static `CompositionDefinition`;
//                          reference-counted so per-worker session caches
//                          and per-frame job payloads share one canonical instance.
//   * `camera_program`  \u2014 shared view of the compiled `camera_v1::CameraProgram`;
//                          the hot path of camera evaluation at every frame.
//                          Reference-counted for the same retention rationale.
//   * `fingerprint`     \u2014 a stable 64-bit hash of the static input that
//                          produced this compile-time snapshot
//                          (CompositionSpec + scene identity + CameraDescriptor
//                          shape + CameraProgram version).  Used as a
//                          cache key in the V2 scene-cache + to detect
//                          cross-version regressions in golden tests.
//
// All members default-construct to a "not yet compiled" state:
//          `definition`/`camera_program` are nullptr; `fingerprint` is 0.
// A "ready" CompiledComposition has all three populated.
// ─────────────────────────────────────────────────────────────────────────────
struct CompiledComposition {
    std::shared_ptr<const CompositionDefinition>  definition{};
    std::shared_ptr<const camera_v1::CameraProgram> camera_program{};
    std::uint64_t                                 fingerprint{0};
};

} // namespace chronon3d
