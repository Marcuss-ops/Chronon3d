#pragma once

/// ============================================================================
/// render_plan_timing.hpp — extract animation.start_frame + duration_frames
/// from a chronon-render-plan.v1 JSON document.
///
/// Cat-3 minimal-surface utility: pure data extraction, no canonical state,
/// no new singleton/registry/cache. Lives in `apps/chronon3d_cli/utils/job/`
/// alongside `render_job.{hpp,cpp}` because the canonical consumer is the
/// CLI render-plan path; C++ downstream consumers may import it via the
/// `chronon3d_cli` link target only (NOT in the public Chronon3D::SDK).
/// ============================================================================

#include <chronon3d/core/types/frame.hpp>

#include <cstdint>

#include <nlohmann/json.hpp>

namespace chronon3d::cli {

/// Animation timing block extracted from the JSON plan. Empty (both fields 0)
/// iff no `animation` block with timing fields was found in any layer AND
/// no layer has top-level timing fields.
struct AnimationTiming {
    chronon3d::Frame start_frame{0};
    chronon3d::Frame duration_frames{0};
};

/// Extract the animation timing block from a render-plan JSON document.
///
/// Resolution order (first-layer-wins, per-field):
///   - For each layer in `plan["layers"]` (in order):
///     * If `layer["animation"]` is an object: prefer its
///       `start_frame` / `duration_frames`. If a field is MISSING in the
///       nested block, fall back to the layer top-level field of the same
///       name.
///     * If `layer["animation"]` is absent: read layer top-level fields.
///   - If any layer yielded at least one present timing field, return
///     immediately (first-layer-wins for that layer's full timing block).
///   - Otherwise return `{0, 0}`.
///
/// `plan` may be any `nlohmann::json` value; missing / wrong-typed fields
/// silently fall through to defaults. Malformed input is NOT validated here
/// — that responsibility belongs to the canonical JSON Schema validator
/// (TICKET-JSON-SCHEMA-VALIDATOR forward-point).
[[nodiscard]] AnimationTiming
extract_animation_timing(const nlohmann::json& plan);

} // namespace chronon3d::cli
