#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>

namespace chronon3d::render_plan {
struct ShadowStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> blur;
    std::array<float, 2> offset{0.0f, 0.0f};
    std::size_t offset_dimensions{0};
};

struct BackdropBlurStyle {
    float blur{16.0f};
    float brightness{1.0f};
    float saturation{1.0f};
    float rim_light{0.0f};
    float refraction{0.0f};
    float surface_noise{0.0f};
    float light_wrap{0.0f};
};

// Per-text halo parameters. This is distinct from ShadowStyle: shadow is a
// translated blurred silhouette, while glow is lowered to GlowParams.
struct GlowStyle {
    float radius{24.0f};
    float intensity{0.25f};
    std::string color{"#FFFFFF"};
};

// P1-10 — authored insertion caret.  Coordinates are byte offsets into the
// layer text; the renderer resolves them against the placed glyph run.
struct TextCaretPlan {
    std::size_t byte_index{0};
    float width{2.0f};
    float height{0.0f};
    std::string color{"#FFFFFF"};
    float opacity{1.0f};
    int blink_period_frames{0};
    float blink_duty{0.5f};
};

struct BackgroundStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::array<float, 2> padding{0.0f, 0.0f};
    std::size_t padding_dimensions{0};
};

// Procedural wear inside the text fill. The renderer expands this into the
// canonical fill PatternTile, so it is deterministic and backend-independent.
struct TextWearStyle {
    float amount{0.5f};
    float scale{8.0f};
    std::uint32_t seed{0};
    float softness{0.25f};
};

// Concrete text paint supplied by the caller. There is deliberately no
// family/weight lookup or preset/profile fallback at the render-plan boundary.
struct LayerStylePlan {
    std::string font;
    std::string font_family;
    int font_weight{400};
    std::string font_style{"normal"};
    // OpenType variable-font coordinates, e.g. "wght=650,wdth=92".
    // Parsed by the compiler into ParagraphStyle::variable_axes.
    std::string variation_axes;
    std::string text_direction{"auto"};
    std::string text_align{"center"};
    std::optional<float> font_size;
    std::string fill;
    // "fast" (default) or "high". High selects the generation-2 128-em MTSDF
    // profile; the value is part of the text style fingerprint.
    std::string quality{"fast"};
    // "fast" (default) or "canonical". Canonical selects the CPU Gaussian
    // union-mask shadow; the native MTSDF route rejects it explicitly.
    std::string shadow_quality{"fast"};
    // "auto" (default), "distance" or "vector". Vector is CPU-only.
    std::string stroke_mode{"auto"};
    bool gel_surface{false};
    // Smoothly fuse glyphs that actually touch/overlap into one GEL dome.
    // Unspecified preserves the historical GEL behavior for old plans.
    std::optional<bool> gel_soft_fusion;
    bool extruded_text{false};
    float extrusion_depth{12.0f};
    bool gel_transmission_enabled{true};
    // GEL material constants. "current" keeps the post-2026-11-08 shading;
    // "reference_20261107" selects the certified 2026-11-07 closure.
    std::string gel_material_profile{"current"};
    bool gel_core{false};
    // 0=material, 1=composite SDF, 2=height, 3=normals, 4=normal validity,
    // 5=gel transmission flag / destination alpha.
    std::uint8_t gel_debug_mode{0};
    // Optional local light override for gel_surface. Azimuth is radians in
    // [-pi, pi]; elevation is the normalized light-vector Z in [0, 1].
    std::optional<float> gel_light_azimuth_radians;
    std::optional<float> gel_light_elevation;
    std::optional<graphics::GradientDefinition> fill_gradient;
    std::optional<TextWearStyle> wear;
    std::shared_ptr<const graphics::PatternTile> fill_pattern;
    std::optional<float> fill_opacity;
    std::string text_fit_mode{"none"};
    std::optional<float> min_font_size;
    std::optional<float> max_font_size;
    std::optional<StrokeStyle> stroke;
    std::shared_ptr<const graphics::PatternTile> stroke_pattern;
    std::optional<ShadowStyle> shadow;
    std::vector<ShadowStyle> shadows;
    std::optional<BackdropBlurStyle> backdrop;
    std::optional<GlowStyle> glow;
    std::optional<BackgroundStyle> background;
    std::optional<TextCaretPlan> text_caret;
    // Layer-wide per-cluster tracking in pixels. This is layout-affecting
    // (it feeds TextFrame::tracking), unlike a per-span paint override.
    // Kept last so existing field offsets remain stable for incremental builds.
    std::optional<float> tracking;
};

// Generic AE-style text animator contract. Editorial motion ids are lowered
// into these primitives before Chronon sees the plan.
struct TextSelectorPlan {
    std::string id;
    std::string unit{"glyph"};
    std::string shape{"smooth"};
    std::string order{"forward"};
    std::string combine{"replace"};
    AnimationTrackPlan start;
    AnimationTrackPlan end;
    AnimationTrackPlan offset;
    AnimationTrackPlan amount;
    bool exclude_spaces{true};
    bool end_inclusive{false};
    bool randomize_order{false};
    std::uint64_t random_seed{0};
    float wiggly_frequency{1.0f};
    float wiggly_amplitude{0.25f};
    std::uint64_t wiggly_seed{0};
    float ease_low{0.0f};
    float ease_high{100.0f};
    float noise_frequency{1.0f};
    float noise_amplitude{0.25f};
    std::uint64_t noise_seed{0};
    std::uint32_t index_step{1};
    std::optional<std::string> semantic_id;

    // AE Range Selector Units and Smoothness.
    std::string range_units{"percentage"};  // "percentage" | "index"
    float smoothness{0.0f};                 // 0..100 % of the active range

    // AE Wiggly Selector temporal controls (shape == "wiggly").
    float wiggles_per_second{0.0f};
    float correlation{0.0f};    // 0..100 %
    float temporal_phase{0.0f}; // degrees
    float spatial_phase{0.0f};  // degrees
    float min_amount{0.0f};     // 0..100 %
    float max_amount{100.0f};   // 0..100 %
    bool lock_dimensions{true};
    std::string wiggly_mode{"smooth"};  // "smooth" | "corner" | "jagged"

    // AE Expression Selector. Variables: textIndex, textTotal, selectorValue,
    // time, frame, fps. The expression result is a 0..100 selector value.
    std::optional<std::string> expression;
};

struct TextAnimatorPlan {
    std::string id;
    std::vector<TextSelectorPlan> selectors;
    std::vector<AnimationTrackPlan> properties;
};

}  // namespace chronon3d::render_plan
