// SPDX-License-Identifier: (project-default)
//
// src/render_graph/nodes/detail/projection_helpers.hpp
//
// Shared 2.5D camera-projection helper for SourceNode + MultiSourceNode
// (TICKET-ae-cam-hash-collision round-2/3 code-reviewer follow-up #2: dedup
// the identical 3-site projection+continue logic in multi_source_node.cpp + the
// 2-site duplication in source_node.cpp).  This header is `src/`-only (NOT
// installed with the SDK) per AGENTS.md v0.1 Cat-3 freeze ("Internal
// implementation details" are not part of the public ABI).
//
// Usage pattern (5 sites across 2 files):
//
//   #include <chronon3d/render_graph/nodes/detail/projection_helpers.hpp>
//
//   if (ctx.frame_input.has_camera_2_5d) {
//       auto matrix_opt = chronon3d::graph::detail::project_to_camera_space(
//           item.matrix, item.opacity, ctx, m_name, "stage_name", item_index);
//       if (!matrix_opt) {
//           // !proj.visible path: skip / return / continue per caller's contract
//           continue;
//       }
//       matrix = *matrix_opt;
//   }
//
// Design notes:
// - The helper computes `canvas_center` + `ssaa_scale` internally from `ctx`
//   so the caller doesn't need to repeat those 2 lines per site.
// - The helper emits the `CHRONON3D_PROJ_DIAG` diagnostic internally (via
//   `spdlog::warn`) so the caller doesn't need to repeat those 10+ lines per
//   site.  Stage tag + item_index are passed as parameters for the diagnostic
//   to identify which site fired the skip.
// - The helper returns the FULLY-COMPOSED matrix
//   (`canvas_center * ssaa_scale * proj.transform.to_mat4()`) so the caller
//   can use `*matrix_opt` directly without further composition.
// - Native 3D shape exclusion (FakeBox3D, GridPlane) is the caller's
//   responsibility (this helper is for the 2D-projection path only — the
//   native 3D shapes still go through `detail::projected_native_3d_bbox`).
// - The helper does NOT include the `m_uses_2_5d_projection` per-node flag
//   check; the caller must gate the call with `ctx.frame_input.has_camera_2_5d`
//   (consistency with the Soluzione B rendering-side pattern).

#pragma once

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <optional>
#include <string>
#include <cmath>

namespace chronon3d::graph::detail {

/// Project a world matrix to camera space for 2.5D scenes.
///
/// Returns the composed matrix (`canvas_center * ssaa_scale * proj.transform.to_mat4()`)
/// on success, or `std::nullopt` if the layer is not visible (behind camera /
/// off frustum — i.e. `project_layer_2_5d` returns `!proj.visible`).
///
/// If env var `CHRONON3D_PROJ_DIAG` is set, emits a `spdlog::warn` diagnostic
/// on `!proj.visible` with `stage` + `node_name` + `item_index` + the full
/// user-spec field set: `proj.{depth, perspective_scale, position, scale}`
/// + `camera.{position, zoom, fov_deg}` + `world_z_depth` + `frame`.  When
/// the env var is unset, the diagnostic is zero-cost (one `getenv` lookup
/// per call; the spdlog::warn is not emitted).
///
/// Parameters:
/// - `world_matrix`: the layer's world matrix (e.g. `item.matrix` for
///   MultiSourceNode or `base_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4())`
///   for SourceNode).
/// - `opacity`: the layer's opacity (e.g. `item.opacity` for MultiSourceNode
///   or `m_opacity_override.value_or(m_node.world_transform.opacity)` for
///   SourceNode).  Passed through to `from_mat4` for the fallback Transform.
/// - `ctx`: the render graph context (provides `frame_input.camera_2_5d`,
///   `frame_input.width/height`, `policy.ssaa_factor`, `sample_time`).
/// - `node_name`: the node's name (for diagnostic `node='{}'` field).
/// - `stage`: optional diagnostic tag (e.g. "predicted_bbox",
///   "text_run_execute", "regular_execute", "execute").  Pass `nullptr` if
///   no per-site tag is needed.
/// - `item_index`: optional per-item index for the diagnostic.  Defaults
///   to `static_cast<std::size_t>(-1)` (no item index, used by SourceNode
///   which is a single-item node).  MultiSourceNode passes the actual
///   per-item index from the for-loop.
inline std::optional<Mat4> project_to_camera_space(
    const Mat4& world_matrix,
    f32 opacity,
    const RenderGraphContext& ctx,
    const std::string& node_name,
    const char* stage = nullptr,
    std::size_t item_index = static_cast<std::size_t>(-1)
) {
    // TICKET-ae-cam-hash-collision forward-fix (code-reviewer round-3/4
    // follow-up #1, option (b)): detect degenerate matrices BEFORE calling
    // `from_mat4` and emit a `spdlog::warn` with caller-specific context
    // (node_name + stage + item_index + opacity + determinant).  This was
    // the root cause of the 3 in-memory FB hash failures + 13 banned PNGs
    // during the 2026-07-07 verification — the same bug at
    // `multi_source_node.cpp:122/216` + `source_node.cpp:122/216` where an
    // empty `chronon3d::Transform tr;` was passed to `project_layer_2_5d`
    // and propagated `layer_size=1x1` to the resolver, causing 2D layers
    // to render as transparent-black.  The check is gated on env var
    // `CHRONON3D_FROM_MAT4_DIAG` (zero cost when unset — one getenv lookup
    // per call).  The detection threshold `std::abs(det) < 1e-6f` catches
    // the common degenerate-matrix case; the rare edge case
    // (non-zero-determinant-but-non-invertible matrix where
    // `glm::decompose` still fails) is not caught here but the silent
    // fallback in `from_mat4` preserves the same semantics as the
    // pre-18b54ca9 behavior.  The `spdlog` include lives in this
    // `src/`-only header (NOT in the public `include/chronon3d/math/
    // transform.hpp`), so the Cat-3 cost (spdlog as direct dep of public
    // math header) flagged by the round-2/3 code-reviewer is fully
    // eliminated.  The `glm::determinant` call is cached in `det` to
    // avoid the 3x-evaluation antipattern flagged by the round-3/4
    // code-reviewer (micro-perf + readability).
    if (std::getenv("CHRONON3D_FROM_MAT4_DIAG")) {
        const f32 det = glm::determinant(world_matrix);
        if (std::isfinite(det) && std::abs(det) < 1e-6f) {
            spdlog::warn(
                "[FROM_MAT4_DIAG] from_mat4 fallback will trigger (matrix det={:.6e} < 1e-6) — node='{}' stage={} item#{} opacity={}",
                static_cast<double>(det),
                node_name,
                stage ? stage : "unknown",
                item_index,
                opacity);
        }
    }
    auto tr = chronon3d::from_mat4(world_matrix, opacity);
    auto proj = chronon3d::project_layer_2_5d(
        tr, world_matrix,
        ctx.frame_input.camera_2_5d,
        static_cast<f32>(ctx.frame_input.width),
        static_cast<f32>(ctx.frame_input.height),
        false);
    if (!proj.visible) {
        if (std::getenv("CHRONON3D_PROJ_DIAG")) {
            spdlog::warn(
                "[PROJ_DIAG] branch=SKIP_NOT_VISIBLE stage={} node='{}' item#{} "
                "world_z_depth={:.1f} proj.depth={:.4f} proj.perspective_scale={:.4f} "
                "proj.position=({:.1f},{:.1f}) proj.scale=({:.4f},{:.4f}) "
                "camera.position=({:.1f},{:.1f},{:.1f}) camera.zoom={:.1f} "
                "camera.fov_deg={:.1f} frame={}",
                stage ? stage : "unknown",
                node_name,
                item_index,
                world_matrix[3][2],
                proj.depth, proj.perspective_scale,
                proj.transform.position.x, proj.transform.position.y,
                proj.transform.scale.x, proj.transform.scale.y,
                ctx.frame_input.camera_2_5d.position.x, ctx.frame_input.camera_2_5d.position.y, ctx.frame_input.camera_2_5d.position.z,
                ctx.frame_input.camera_2_5d.zoom, ctx.frame_input.camera_2_5d.fov_deg,
                ctx.frame_input.sample_time.integral_frame());
        }
        return std::nullopt;
    }
    const Mat4 ssaa_scale = glm::scale(Mat4(1.0f), Vec3(ctx.policy.ssaa_factor, ctx.policy.ssaa_factor, 1.0f));
    const Mat4 canvas_center = glm::translate(Mat4(1.0f), Vec3(ctx.frame_input.width * 0.5f, ctx.frame_input.height * 0.5f, 0.0f));
    return canvas_center * ssaa_scale * proj.transform.to_mat4();
}

} // namespace chronon3d::graph::detail
