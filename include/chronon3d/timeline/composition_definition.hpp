#pragma once

// ============================================================================
// include/chronon3d/timeline/composition_definition.hpp
//
// P3-C (V0.2 timeline staging) — `CompositionDefinition` lives NEXT to
// the legacy `chronon3d::Composition` class.  This commit does NOT
// replace or migrate the legacy surface; the new struct is introduced
// in parallel as the structural root for the upcoming V2 pipeline.
//
// Layout (per V2 staging contract — see docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md
// §timeline V2 and the PASS A/B/C notes in the orchestrator):
//
//   chronon3d::CompositionDefinition   (this file)
//        ├── chronon3d::CompositionSpec          (legacy reuse)
//        ├── chronon3d::SceneFunction            (alias declared here;
//        │                                         redundant `using` of the
//        │                                         legacy alias for stable
//        │                                         local naming inside the
//        │                                         struct body)
//        └── std::optional<camera_v1::CameraDescriptor>
//             (V2 substitution for the legacy `Composition::default_camera_descriptor(...)`;
//             `nullopt` means: identity / 2.5D null-rig, equivalent to the legacy
//             `Composition::default_camera_2_5d(...)` path)
//
// Anti-DRY note (Rule 4 ANTI_DUPLICATION_RULES):
//   The legacy `Composition` keeps its own `CompositionSpec` + CameraDescriptor
//   members + `*_default_camera_*` API.  `CompositionDefinition` is a NEW struct,
//   not a migration.  Migration to the V2 surface (PASS B/C of the V0.2 plan)
//   will remove the legacy duplication in follow-up commits.
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
//   V2 staging struct.  Sits NEXT to (does NOT replace) the legacy
//   `chronon3d::Composition`.  Holds the static recipe of one composition:
//     * `composition` \u2014 the timing/timeline-bound `CompositionSpec` (re-uses the
//       legacy struct verbatim \u2014 single-source-of-truth for timeline keys).
//     * `scene`       \u2014 a `SceneFunction` (signature `Scene(const FrameContext&)`)
//                       the V2 driver invokes per frame to materialise a Scene.
//     * `camera`      \u2014 a V1-shape authoring descriptor (camera_v1::CameraDescriptor)
//                       when set; `std::nullopt` falls back to identity / 2.5D null-rig
//                       (legacy Composition::default_camera_2_5d() path).
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
