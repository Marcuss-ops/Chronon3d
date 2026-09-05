#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// native_promotion.hpp — pure predicates/transforms shared by the native
// (GPU) promotion fast-paths in render nodes and the Vulkan backend.
//
// PURPOSE
//   Every try_native_* gate re-derived the same five properties by hand:
//   active mask, axis-aligned-affine placement, premultiplied fill color,
//   clip intersection and surface lifecycle choreography. The variants had
//   already drifted (some check the [0][1]/[1][0] shear terms, some also the
//   w-row camera terms). These helpers give the invariants ONE definition.
//
// SCOPE RULE (P1.2): these helpers answer "how do I compute a property?".
// They MUST NOT grow into a NativePromotionResolver/Registry/Manager — the
// decision "which execution path runs" belongs to the canonical execution
// policy, not to a helper.
//
// SEMANTICS FREEZE: each helper reproduces the strictest pre-existing
// variant. Callers that intentionally need a weaker check (e.g. the
// is_line exception for ShapeType::Line) pass explicit parameters — the
// helper never silently weakens a gate.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <algorithm>
#include <optional>

namespace chronon3d::graph::native_promotion {

/// Tolerance shared by every affine-classification gate. Do not introduce a
/// second epsilon for the same purpose.
inline constexpr f32 kAffineEpsilon = 1e-4f;

/// True when `state` carries an active (masking) layer mask. A node with an
/// active mask is never promotable to a mask-unaware native kernel.
[[nodiscard]] inline bool has_active_mask(const RenderState& state) noexcept {
    return state.mask && state.mask->enabled();
}

/// True when the matrix is a plain 2D translation/scale (no rotation/shear
/// in the xy terms, no camera projection in the w-row). This is the STRICT
/// gate: it includes both the shear check [0][1]/[1][0] and the w-row check
/// [0][3]/[1][3]/[3][3].
/// `check_shear=false` reproduces the legacy Line variant that deliberately
/// tolerates the [0][1]/[1][0] terms (diagonal lines are affine-rendered).
[[nodiscard]] inline bool is_axis_aligned_affine(
    const RenderState& state, bool check_shear = true) noexcept {
    if (check_shear &&
        (std::abs(state.matrix[0][1]) > kAffineEpsilon ||
         std::abs(state.matrix[1][0]) > kAffineEpsilon)) {
        return false;
    }
    return std::abs(state.matrix[0][3]) <= kAffineEpsilon &&
           std::abs(state.matrix[1][3]) <= kAffineEpsilon &&
           std::abs(state.matrix[3][3] - 1.0f) <= kAffineEpsilon;
}

/// True when the w-row carries a camera/perspective projection ([0][3],
/// [1][3] non-zero or [3][3] != 1). The z-cross terms only affect z and are
/// irrelevant to 2D rect fills — deliberately NOT checked here.
[[nodiscard]] inline bool is_projected(const RenderState& state) noexcept {
    return std::abs(state.matrix[0][3]) > kAffineEpsilon ||
           std::abs(state.matrix[1][3]) > kAffineEpsilon ||
           std::abs(state.matrix[3][3] - 1.0f) > kAffineEpsilon;
}

/// Premultiplied fill color exactly as the CPU rasterizer stores it:
/// to_linear(), alpha *= opacity, then premultiply rgb.
/// `out_alpha_zero` reports alpha <= 0 so callers can apply their legacy
/// early-return semantics without re-deriving the color here.
[[nodiscard]] inline Color premultiply(
    const Color& straight, f32 opacity, bool* out_alpha_zero = nullptr) {
    Color c = straight.to_linear();
    c.a *= opacity;
    if (out_alpha_zero) *out_alpha_zero = c.a <= 0.0f;
    c.r *= c.a;
    c.g *= c.a;
    c.b *= c.a;
    return c;
}

/// Intersect a pixel rect with an optional clip box, clamped to the
/// framebuffer bounds. Returns false when the intersection is empty.
[[nodiscard]] inline bool intersect_clip(
    const std::optional<raster::BBox>& clip,
    i32 framebuffer_width, i32 framebuffer_height,
    i32& x0, i32& y0, i32& x1, i32& y1) noexcept {
    x0 = std::clamp(x0, 0, framebuffer_width);
    y0 = std::clamp(y0, 0, framebuffer_height);
    x1 = std::clamp(x1, 0, framebuffer_width);
    y1 = std::clamp(y1, 0, framebuffer_height);
    if (clip) {
        x0 = std::max(x0, clip->x0);
        y0 = std::max(y0, clip->y0);
        x1 = std::min(x1, clip->x1);
        y1 = std::min(y1, clip->y1);
    }
    return x0 < x1 && y0 < y1;
}

} // namespace chronon3d::graph::native_promotion
