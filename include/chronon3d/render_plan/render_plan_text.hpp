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
};

// Per-text halo parameters. This is distinct from ShadowStyle: shadow is a
// translated blurred silhouette, while glow is lowered to GlowParams.
struct GlowStyle {
    float radius{24.0f};
    float intensity{0.25f};
    std::string color{"#FFFFFF"};
};

struct BackgroundStyle {
    std::string color;
    std::optional<float> opacity;
    std::optional<float> radius;
    std::array<float, 2> padding{0.0f, 0.0f};
    std::size_t padding_dimensions{0};
};

// Concrete text paint supplied by the caller. There is deliberately no
// family/weight lookup or preset/profile fallback at the render-plan boundary.
struct LayerStylePlan {
    std::string font;
    std::string font_family;
    int font_weight{400};
    std::string font_style{"normal"};
    std::string text_direction{"auto"};
    std::string text_align{"center"};
    std::optional<float> font_size;
    std::string fill;
    std::optional<graphics::GradientDefinition> fill_gradient;
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
    bool randomize_order{false};
    std::uint64_t random_seed{0};
    float wiggly_frequency{1.0f};
    float wiggly_amplitude{0.25f};
    std::uint64_t wiggly_seed{0};
    float noise_frequency{1.0f};
    float noise_amplitude{0.25f};
    std::uint64_t noise_seed{0};
    std::uint32_t index_step{1};
    std::optional<std::string> semantic_id;
};

struct TextAnimatorPlan {
    std::string id;
    std::vector<TextSelectorPlan> selectors;
    std::vector<AnimationTrackPlan> properties;
};

}  // namespace chronon3d::render_plan
