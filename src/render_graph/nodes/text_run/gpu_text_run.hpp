// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// gpu_text_run.hpp — canonical GPU text-run lowering contract.
//
// Internal-only header: lives under src/render_graph/nodes/text_run/, NOT in
// include/chronon3d/. Native text is lowered through the prepared MTSDF atlas;
// unsupported shapes fail closed and are handed to the explicit software
// fallback owned by text_run_execution.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/runtime/gpu_text_run_cache.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace chronon3d::runtime { class GpuGlyphAtlas; }

namespace chronon3d::graph::text_run {

/// GPU placement for one glyph when its animated state is submitted as a
/// separate MTSDF run.
///
/// Prepared glyph planes keep their immutable coordinates in run/layout
/// space.  A per-glyph scale must therefore be applied to the glyph's ink
/// around its layout origin, exactly like the software text matrix does.  If
/// the run translation were left unchanged, scaling a glyph at x=500 would
/// also move it by 500 * (scale - 1) pixels; the resulting frame-to-frame
/// drift is visible as text jitter during phrase emphasis animations.
struct GpuGlyphPlacement {
    float tx{0.0f};
    float ty{0.0f};
    float sx{1.0f};
    float sy{1.0f};
};

/// Screen-space offset that makes the MTSDF shader transform pivot on
/// `layout_position + anchor` instead of the glyph layout origin.
///
/// For a glyph-plane point p the shader computes
///   screen = run.t + animator_position + run.s * origin
///            + run.s * animator_scale * rotation * shear * (p - origin).
/// A per-glyph scale/rotation therefore pivots on the glyph origin unless the
/// record compensates. This returns
///   run.s * (I - animator_scale * rotation * shear) * anchor,
/// which leaves `layout_position + anchor` fixed and reduces to the previous
/// `anchor * (1 - scale)` behaviour when rotation and shear are zero.
/// Per-glyph X/Y rotations at or beyond the old 89-degree MTSDF shear limit
/// use the exact orthographic projection matrix instead of tan() shears.
/// Keeping the threshold in one place makes the host pivot compensation,
/// predicate and both compute shaders agree on the representation.
inline constexpr float kGpuExact3DRotationThreshold = 89.0f;
inline constexpr std::uint32_t kTextRunEffectExact3DRotation = 1u << 11;

[[nodiscard]] inline bool gpu_uses_exact_3d_rotation(
    const GlyphInstanceState& glyph) noexcept {
    return std::abs(glyph.rotation.x) >= kGpuExact3DRotationThreshold ||
           std::abs(glyph.rotation.y) >= kGpuExact3DRotationThreshold;
}

[[nodiscard]] inline Vec2 gpu_anchor_pivot_offset(
    const GlyphInstanceState& glyph,
    float animator_scale_x,
    float animator_scale_y,
    float rotation_z_degrees,
    float run_sx,
    float run_sy) noexcept {
    const float ax = glyph.anchor.x;
    const float ay = glyph.anchor.y;
    if (ax == 0.0f && ay == 0.0f) return Vec2{0.0f, 0.0f};

    constexpr double kPi = 3.14159265358979323846;
    const double rz =
        static_cast<double>(rotation_z_degrees) * (kPi / 180.0);
    const double c = std::cos(rz);
    const double s = std::sin(rz);
    double rs00 = 0.0;
    double rs01 = 0.0;
    double rs10 = 0.0;
    double rs11 = 0.0;
    if (gpu_uses_exact_3d_rotation(glyph)) {
        // Orthographic projection of a plane after Rx * Ry. The shader undoes
        // Rz and then this matrix, so the forward composition here is Rz * M.
        const double ry = static_cast<double>(glyph.rotation.y) * (kPi / 180.0);
        const double rx = static_cast<double>(glyph.rotation.x) * (kPi / 180.0);
        const double cx = std::cos(rx);
        const double sx = std::sin(rx);
        const double cy = std::cos(ry);
        const double sy = std::sin(ry);
        if (std::abs(cx * cy) < 1e-6) return Vec2{0.0f, 0.0f};
        rs00 = c * cy - s * sx * sy;
        rs01 = -s * cx;
        rs10 = s * cy + c * sx * sy;
        rs11 = c * cx;
    } else {
        const double ry = std::clamp(
            static_cast<double>(glyph.rotation.y) * (kPi / 180.0),
            -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
        const double rx = std::clamp(
            static_cast<double>(glyph.rotation.x) * (kPi / 180.0),
            -kPi / 2.0 + 1e-3, kPi / 2.0 - 1e-3);
        const double shx = std::tan(ry);
        const double shy = std::tan(rx);

        // The shader inverts the rotation first and the shear second, so the
        // forward per-glyph transform is rotation * shear.
        rs00 = c - s * shy;
        rs01 = c * shx - s;
        rs10 = s + c * shy;
        rs11 = s * shx + c;
    }

    const double n00 =
        static_cast<double>(animator_scale_x) * rs00;
    const double n01 =
        static_cast<double>(animator_scale_x) * rs01;
    const double n10 =
        static_cast<double>(animator_scale_y) * rs10;
    const double n11 =
        static_cast<double>(animator_scale_y) * rs11;

    return Vec2{
        static_cast<float>(static_cast<double>(run_sx) *
            (ax - (n00 * ax + n01 * ay))),
        static_cast<float>(static_cast<double>(run_sy) *
            (ay - (n10 * ax + n11 * ay)))};
}

[[nodiscard]] inline GpuGlyphPlacement make_gpu_glyph_placement(
    const GlyphInstanceState& glyph,
    float run_tx,
    float run_ty,
    float run_sx,
    float run_sy) noexcept {
    // Scale.z is the existing text 2.5D uniform scale.  Keeping it here
    // makes the GPU placement match build_glyph_matrix() for the axes that
    // this MTSDF path supports (rotation/skew are rejected separately).
    const float glyph_scale_x = glyph.scale.x * glyph.scale.z;
    const float glyph_scale_y = glyph.scale.y * glyph.scale.z;

    // The immutable plane already contains layout_position.  Compensate the
    // run origin so only the glyph-local ink is scaled, while position and
    // baseline/anchor offsets remain in the same space as the CPU renderer.
    const float origin_x = glyph.position.x;
    const float origin_y = glyph.position.y + glyph.baseline_shift;
    return GpuGlyphPlacement{
        run_tx + run_sx * (origin_x +
                           (glyph.layout_position.x + glyph.anchor.x) *
                               (1.0f - glyph_scale_x)),
        run_ty + run_sy * (origin_y +
                           (glyph.layout_position.y + glyph.anchor.y) *
                               (1.0f - glyph_scale_y)),
        run_sx * glyph_scale_x,
        run_sy * glyph_scale_y};
}

/// Return true when the already-resolved glyph states fit the native MTSDF
/// dynamic-record contract. Animator evaluation itself stays in the canonical
/// text driver; resolved records are uploaded per glyph for native rendering.
[[nodiscard]] bool can_lower_gpu_text_animation(const TextRunShape& shape) noexcept;

/// True when the run-level emphasis profile can be sampled directly into one
/// TextRunDynamic record (one word, one selector, no per-glyph variation).
[[nodiscard]] bool can_lower_gpu_run_level_animation(const TextRunShape& shape) noexcept;

/// Sample the run-level emphasis profile into the caller-owned dynamic record.
/// The caller initializes transform/opacity/color before invoking this helper;
/// the animation values are composed onto those fields.
void sample_gpu_run_level_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

/// Legacy run-level selector sampler kept for callers that explicitly use
/// the compact single-selector contract. The MTSDF TextRun path now uploads
/// the canonical resolved GlyphInstanceState values per glyph.
void sample_gpu_text_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

// Samples the run-uniform GEL light animator into the reserved packed light
// parameter lane. This is separate from per-glyph animation because a single
// directional light must shade the composed TextRun surface uniformly.
void sample_gpu_gel_light_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept;

/// Resolve and dispatch the prepared MTSDF representation of an already-shaped
/// text run. If the native contract is unavailable this returns an explicit
/// error; the caller owns the software fallback policy.
[[nodiscard]] graph::RenderOpResult draw_cached_text_run(
    RenderGraphContext& ctx,
    Framebuffer& destination,
    const TextRunShape& shape,
    const glm::mat4& model_matrix,
    float opacity);

/// Prepare the MTSDF representation of a TextRun into the persistent GPU glyph atlas
/// and return a prepared record suitable for cached dispatch.
[[nodiscard]] std::shared_ptr<const runtime::GpuPreparedTextRun> prepare_mtsdf_text_run(
    runtime::GpuGlyphAtlas& glyph_atlas,
    const assets::AssetResolver* resolver,
    const TextRunShape& shape);

} // namespace chronon3d::graph::text_run
