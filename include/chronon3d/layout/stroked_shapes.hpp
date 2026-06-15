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

inline void draw_stroked_circle(LayerBuilder& l,
                                std::string name,
                                f32 radius,
                                Color fill_color,
                                Color stroke_color,
                                f32 stroke_width) {
    CircleParams p;
    p.radius = radius;
    p.color = fill_color;
    p.fill = FillStyle::solid(fill_color);
    p.stroke.enabled = stroke_width > 0.0f;
    p.stroke.color = stroke_color;
    p.stroke.width = stroke_width;
    p.stroke.alignment = StrokeAlignment::Center;
    l.circle(std::move(name), p);
}

inline void draw_premium_pill(LayerBuilder& l, std::string name, const PremiumPillStyle& style) {
    draw_stroked_rounded_rect(
        l,
        std::move(name),
        style.size,
        style.radius,
        style.fill,
        style.stroke,
        style.stroke_width,
        style.stroke_alignment
    );
}

// ── Decorative helpers ──────────────────────────────────────────────────────

inline void draw_premium_asterisk(LayerBuilder& l,
                                  std::string name,
                                  Vec3 pos,
                                  Color color,
                                  f32 outer_radius = 42.0f,
                                  f32 inner_radius = 12.0f) {
    l.star(std::move(name), {
        .center = {pos.x, pos.y},
        .points = 8,
        .inner_radius = inner_radius,
        .outer_radius = outer_radius,
        .color = color
    });
}

inline void draw_sparkle_4point(LayerBuilder& l,
                                std::string name,
                                Vec3 pos,
                                Color color,
                                f32 size = 30.0f) {
    l.star(std::move(name), {
        .center = {pos.x, pos.y},
        .points = 4,
        .inner_radius = size * 0.22f,
        .outer_radius = size,
        .color = color
    });
}

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

inline void apply_post_pipeline(LayerBuilder& l, const PostEffectPipeline& pipe) {
    if (pipe.enable_bloom) {
        l.bloom(pipe.bloom_threshold, pipe.bloom_radius, pipe.bloom_intensity);
    }
    if (pipe.enable_tint) {
        l.tint(pipe.tint_color, pipe.tint_amount);
    }
}

// ── Pre-built TextMaterial presets ──────────────────────────────────────────

namespace Materials {

inline TextMaterial HotPinkFlatGlow() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {1.0f, 0.0f, 0.78f, 1.0f};
    m.bottom_color = {0.8f, 0.0f, 0.6f, 1.0f};
    m.emissive = 1.2f;
    return m;
}

inline TextMaterial GlossyBlueTitle() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {0.0f, 0.8f, 1.0f, 1.0f};
    m.bottom_color = {0.0f, 0.4f, 0.8f, 1.0f};
    m.bevel_px = 1.5f;
    m.emissive = 1.1f;
    return m;
}

inline TextMaterial DarkGlassTitle() {
    TextMaterial m;
    m.enabled = true;
    m.top_color = {0.98f, 0.95f, 1.0f, 0.92f};
    m.bottom_color = {0.80f, 0.82f, 0.95f, 0.78f};
    m.bevel_px = 1.0f;
    m.top_highlight_opacity = 0.20f;
    m.bottom_shade_opacity = 0.10f;
    m.use_material_glow = true;
    m.glow_radius = 10.0f;
    m.glow_intensity = 0.45f;
    m.glow_color = {0.72f, 0.56f, 1.0f, 0.60f};
    return m;
}

} // namespace Materials

} // namespace chronon3d
