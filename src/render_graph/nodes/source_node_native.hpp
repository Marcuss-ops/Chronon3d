#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// source_node_native.hpp — shared internals for the SourceNode translation
// units.
//
// source_node.cpp used to be a single ~720-line TU.  It is now split by
// concern (native image fast path, native rect fast path, geometry/cache
// queries, frame execution).  This header carries the few symbols that more
// than one of those TUs needs:
//
//   - kSeedFrameEpsilon / nearly_equal()  — tolerance shared by the seed
//     predicate (execute TU) and the full-frame coverage test (geometry TU).
//   - covers_full_frame_as_rectangle()    — definition lives in the geometry
//     TU; used by execute + can_seed_full_frame.
//   - try_native_rect_fill()              — GPU fast-path entry point;
//     definition lives in the native-rect TU.
//
// try_native_image() stays declared in the public source_node.hpp
// (graph::detail surface, also called by MultiSourceNode); its definition
// lives in source_node_native_image.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/nodes/source_node.hpp>

#include <cmath>

namespace chronon3d::graph::detail {

inline constexpr f32 kSeedFrameEpsilon = 1e-3f;

[[nodiscard]] inline bool nearly_equal(f32 a, f32 b, f32 eps = kSeedFrameEpsilon) {
    return std::abs(a - b) <= eps;
}

// True iff `matrix` maps the `width` x `height` rectangle exactly onto the
// canvas [0,w]x[0,h] (or the centered [-w/2,w/2] variant).  Defined in
// source_node_geometry.cpp.
[[nodiscard]] bool covers_full_frame_as_rectangle(
    const Mat4& matrix, f32 width, f32 height, bool centered = false);

// GPU solid-rect fast path — defined in source_node_native_rect.cpp.
[[nodiscard]] bool try_native_rect_fill(
    RenderGraphContext& ctx, Framebuffer& fb,
    const ::chronon3d::RenderNode& node, const RenderState& state);

} // namespace chronon3d::graph::detail
