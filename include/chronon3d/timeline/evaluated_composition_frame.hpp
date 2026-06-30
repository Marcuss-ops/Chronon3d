#pragma once

// ============================================================================
// include/chronon3d/timeline/evaluated_composition_frame.hpp
//
// P3-C (V0.2 timeline staging) — `EvaluatedCompositionFrame` lives NEXT to
// the legacy `Composition` class.  It is the V2 staging OUTPUT of one
// composition evaluated at one FrameContext point in time:
//
//   - `scene`   \u2014 the fully-resolved `Scene` for this frame (consumers
//                can route directly to the V2 render driver).
//   - `camera`  \u2014 optionally the fully-resolved `Camera2_5D` for this
//                frame; `std::nullopt` means identity / 2.5D null-rig
//                (legacy Composition::default_camera_2_5d() path).
//
// Anti-DRY note (Rule 4 ANTI_DUPLICATION_RULES):
//   There is a `chronon3d::timeline::composition_evaluation.hpp` helper
//   in the existing surface that emits `Scene + optional<Camera2_5D>`
//   for a given `Composition`.  `EvaluatedCompositionFrame` is a typed
//   POD-version of the same shape \u2014 it does NOT subsume the evaluation
//   helper; both coexist.  V2 promotion will converge them.
#include <optional>

#include <chronon3d/scene/model/core/scene.hpp>                  // Scene (canonical)
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>          // Camera2_5D

namespace chronon3d {

// ─────────────────────────────────────────────────────────────────────────────
// chronon3d::EvaluatedCompositionFrame
//
//   V2 staging struct.  Sits NEXT to (does NOT replace) anything \u2014 it is the
//   typed result of `CompiledComposition::evaluate(frame)` once that method
//   materialises (this struct is the output shape, not the function itself).
//
//   * `scene`    \u2014 the resolved `Scene` for this frame.
//   * `camera`   \u2014 the resolved `Camera2_5D` for this frame, or `std::nullopt`
//                  for the identity / 2.5D null-rig fallback.
//
//   Surface-cost note:
//     `_scene_` brings in the Scene subtree (model/core) and `_Camera2_5D_`
//     brings in GLM math templates.  Acceptable for the V0.2 stage-in.
// ─────────────────────────────────────────────────────────────────────────────
struct EvaluatedCompositionFrame {
    Scene                          scene{};
    std::optional<Camera2_5D>      camera{};
};

} // namespace chronon3d
