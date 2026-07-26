#pragma once

// ============================================================================
// include/chronon3d/timeline/evaluated_composition_frame.hpp
//
// P3-C (V0.2 timeline staging) — `EvaluatedCompositionFrame` is the typed
// output of one
// composition evaluated at one FrameContext point in time:
//
//   - `scene`   \u2014 the fully-resolved `Scene` for this frame (consumers
//                can route directly to the V2 render driver).
//   - `camera`  \u2014 optionally the fully-resolved `Camera2_5D` for this
//                frame; `std::nullopt` means identity / 2.5D null-rig
//                (an absent authored camera).
//
// The result is intentionally a value object so callers do not need a
// second composition-evaluation helper with a parallel return shape.
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
