#pragma once

// ============================================================================
// include/chronon3d/timeline/composition_definition.hpp
//
// P3-C (V0.2 timeline) — `CompositionDefinition` is the explicit immutable
// input for the compiled composition pipeline.
//
// Layout (per V2 staging contract — see docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md
// §timeline V2 and the PASS A/B/C notes in the orchestrator):
//
//   chronon3d::CompositionDefinition   (this file)
//        ├── chronon3d::CompositionSpec          (shared value spec)
//        ├── chronon3d::SceneFunction            (local alias for the
//        │                                         canonical scene callback)
//        └── std::optional<camera_v1::CameraDescriptor>
//             (optional canonical camera descriptor; `nullopt` means no
//             authored camera)
//
// Anti-DRY note (Rule 4 ANTI_DUPLICATION_RULES):
//   The scene-function Composition remains available for authoring, while
//   this definition is the explicit value passed to compilation.
//
// Surface-cost note:
//   Including this header drags `camera_v1/CameraDescriptor`, `Composition`,
//   `CompositionSpec`, `FrameContext`, and the std::function machinery into
//   the includer's TU.  Acceptable for the V0.2 stage-in; revisit when the
//   new struct becomes a hot per-frame allocation target.
// ============================================================================

#include <functional>
#include <optional>

#include <chronon3d/timeline/composition.hpp>      // CompositionSpec, FrameContext, Scene
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>  // camera_v1::CameraDescriptor

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::CompositionDefinition
//
//   Explicit compiled-pipeline input. Holds the static recipe of one
//   composition:
//     * `composition` \u2014 the timing/timeline-bound `CompositionSpec`.
//     * `scene`       \u2014 a `SceneFunction` (signature `Scene(const FrameContext&)`)
//                       the V2 driver invokes per frame to materialise a Scene.
//     * `camera`      \u2014 a V1-shape authoring descriptor (camera_v1::CameraDescriptor)
//                       when set; `std::nullopt` falls back to identity / 2.5D null-rig
//                       (no authored camera path).
//
//   Move-and-copy friendly: trivially copyable + trivially destructible assuming
//   `SceneFunction` (std::function) is the only non-trivial member and the std::function
//   captures remain state-safe under copy.  No virtual methods, no inheritance.
// ─────────────────────────────────────────────────────────────────────────────
struct CompositionDefinition {
    using SceneFunction = std::function<Scene(const FrameContext&)>;

    CompositionSpec composition{};
    SceneFunction    scene{};
    std::optional<camera_v1::CameraDescriptor> camera{};
};

} // namespace chronon3d
