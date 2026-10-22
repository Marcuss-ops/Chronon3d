#pragma once

// ---------------------------------------------------------------------------
// stroked_shapes.hpp
//
// Stroked shape drawing helpers, post-effect pipeline, decorative star/
// sparkle helpers, and pre-built TextMaterial presets.
//
// Extracted from design_kit.hpp for single-responsibility.
// ---------------------------------------------------------------------------

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/text_material.hpp>

#include <string>

namespace chronon3d {

// ── Premium pill style ──────────────────────────────────────────────────────

struct PremiumPillStyle {
    Vec2 size{900.0f, 120.0f};
    f32 radius{60.0f};
    Color fill{0.03f, 0.01f, 0.05f, 0.14f};
    Color stroke{0.78f, 0.48f, 0.88f, 0.28f};
    f32 stroke_width{1.0f};
    StrokeAlignment stroke_alignment{StrokeAlignment::Center};
};

// ── Stroked shape drawing ───────────────────────────────────────────────────

inline void draw_stroked_rounded_rect(LayerBuilder& l,
                                      std::string name,
                                      Vec2 size,
                                      f32 radius,
                                      Color fill_color,
                                      Color stroke_color,
                                      f32 stroke_width,
                                      StrokeAlignment stroke_alignment = StrokeAlignment::Center) {
    RoundedRectParams p;
    p.size = size;
    p.radius = radius;
    p.color = fill_color;
    p.fill = FillStyle::solid(fill_color);
    p.stroke.enabled = stroke_width > 0.0f;
    p.stroke.color = stroke_color;
    p.stroke.width = stroke_width;
    p.stroke.alignment = stroke_alignment;
    l.rounded_rect(std::move(name), p);
}



// ── Decorative helpers ──────────────────────────────────────────────────────



// ── Post-effect pipeline ────────────────────────────────────────────────────

struct PostEffectPipeline {
    bool enable_bloom{false};
    f32 bloom_threshold{0.8f};
    f32 bloom_radius{24.0f};
    f32 bloom_intensity{0.6f};
    bool enable_tint{false};
    Color tint_color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 tint_amount{0.0f};
};


// ── Pre-built TextMaterial presets ──────────────────────────────────────────

namespace Materials {




} // namespace Materials

} // namespace chronon3d
