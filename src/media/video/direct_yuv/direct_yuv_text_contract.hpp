#pragma once

// =============================================================================
// direct_yuv_text_contract.hpp — the DirectYUV text lowering contract
//
// Internal (not installed). One authority decides what a prepared TextRunShape
// means to the DirectYUV lane: whether its canonical layout/MTSDF contract is
// established, whether its remaining style semantics are representable, and the
// canonical single drop shadow the CUDA kernel resamples from that same
// prepared distance field.
//
// Two consumers, one rule:
//   * direct_yuv_capability.cpp fills LayerYuvQuery from this classifier, so
//     the declared capability matrix (composition_capability.hpp) is the only
//     place the lane decision is made.
//   * direct_yuv_program.cpp lowers the same shadow into the layer resource, so
//     a shadow admitted by the matrix can never be silently dropped at render.
//
// A shadow is NOT a second rasterization: the prepared MTSDF atlas already
// encodes the silhouette as a signed distance, so the kernel samples it at the
// authored offset with the authored softness. More than one enabled shadow,
// gradient paint, premium material and animated/dissolving documents stay
// explicit FullGraph fallbacks, never approximations.
// =============================================================================

#include <chronon3d/math/color.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <cmath>
#include <cstddef>

namespace chronon3d::media::video {

/// Canonical single drop shadow lowered into the DirectYUV layer resource.
struct DirectYuvTextShadow {
    bool enabled{false};
    float offset_x{0.0f};
    float offset_y{0.0f};
    float blur{0.0f};
    /// Authored shadow opacity. Deliberately NOT folded into the colour alpha:
    /// the canonical text renderer applies it once to the soft coverage.
    float opacity{1.0f};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

/// Resolved representation of one prepared text run on the DirectYUV lane.
struct DirectYuvTextContract {
    /// The canonical PreparedTextRun/MTSDF layout contract is established.
    /// Mirrors `LayerYuvQuery::prepared_mtsdf`.
    bool prepared_mtsdf{false};
    /// The remaining style semantics lower faithfully. Mirrors
    /// `LayerYuvQuery::text_style_representable`.
    bool style_representable{false};
    /// Stable rejection class, or nullptr when the run is representable.
    const char* rejection{nullptr};
    DirectYuvTextShadow shadow{};
};

/// Classify one prepared text run for the DirectYUV lane. Pure, allocation-free
/// and safe to call in preparation as well as in capability resolution.
[[nodiscard]] inline DirectYuvTextContract resolve_direct_yuv_text_contract(
    const TextRunShape& shape) noexcept {
    DirectYuvTextContract contract;
    if (!shape.layout) {
        contract.rejection = "text_missing_canonical_layout";
        return contract;
    }
    if (shape.layout->placed.glyphs.empty()) {
        contract.rejection = "text_layout_has_no_glyphs";
        return contract;
    }
    if (shape.animated_doc) {
        contract.rejection = "text_animated_document_unsupported";
        return contract;
    }
    if (shape.dissolve_layout || !shape.dissolve_glyphs.empty()) {
        contract.rejection = "text_dissolve_unsupported";
        return contract;
    }
    for (const auto& animator : shape.animators) {
        if (animator.enabled) {
            contract.rejection = "text_glyph_animation_unsupported";
            return contract;
        }
    }
    if (shape.layout->font_spans.empty()) {
        contract.rejection = "text_missing_font_spans";
        return contract;
    }
    // From here on the prepared MTSDF contract is established; only the
    // remaining style semantics decide whether the lowering is faithful.
    contract.prepared_mtsdf = true;

    // Canonical single drop shadow. Zero enabled shadows is the plain lane;
    // exactly one is resampled from the prepared distance field; more than one
    // (a multi-layer glow) has no single-field representation.
    std::size_t enabled_shadows = 0;
    const TextShadow* single_shadow = nullptr;
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        ++enabled_shadows;
        single_shadow = &shadow;
    }
    if (enabled_shadows > 1) {
        contract.rejection = "text_multiple_shadows_unsupported";
        return contract;
    }
    if (single_shadow) {
        const auto finite = [](float value) { return std::isfinite(value); };
        if (!finite(single_shadow->offset.x) || !finite(single_shadow->offset.y) ||
            !finite(single_shadow->blur) || !finite(single_shadow->opacity) ||
            single_shadow->blur < 0.0f || single_shadow->opacity < 0.0f) {
            contract.rejection = "text_shadow_invalid";
            return contract;
        }
        // A fully transparent shadow is authored-but-invisible: it composes to
        // nothing and must not force the whole clip off the fast lane.
        if (single_shadow->opacity > 0.0f && single_shadow->color.a > 0.0f) {
            contract.shadow.enabled = true;
            contract.shadow.offset_x = single_shadow->offset.x;
            contract.shadow.offset_y = single_shadow->offset.y;
            contract.shadow.blur = single_shadow->blur;
            contract.shadow.opacity = single_shadow->opacity;
            contract.shadow.color = single_shadow->color;
        }
    }

    if (shape.paint.fill_style || shape.paint.stroke_style) {
        contract.rejection = "text_gradient_paint_unsupported";
        return contract;
    }
    if (shape.material.enabled) {
        contract.rejection = "text_material_unsupported";
        return contract;
    }
    contract.style_representable = true;
    return contract;
}

} // namespace chronon3d::media::video
