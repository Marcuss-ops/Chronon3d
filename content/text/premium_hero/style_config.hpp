#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/fill.hpp>

#include "text_premium_hero_shared.hpp"

namespace chronon3d::content::text::style {

// ═══════════════════════════════════════════════════════════════════════════════
// StyleConfig — data-driven style definition
// ═══════════════════════════════════════════════════════════════════════════════
// Each palette defines:
//   • 1 background colour     (bg / bg_gradient)
//   • 1 primary accent        (hero title, button fill)
//   • 1 secondary accent      (subtitle, decorative shapes)
//   • 1 accent glow           (glow / bloom colour)
//   • 1 neutral text          (subtitle / body text)
//   • Typography sizes         (hero, subtitle, button)
//   • Visual FX scalars        (glow, shadow, blur intensity)
// ═══════════════════════════════════════════════════════════════════════════════

struct Palette {
    Color bg{0.02f, 0.02f, 0.05f, 1.0f};
    Color bg_gradient{0.01f, 0.01f, 0.03f, 1.0f};
    Color primary{1.0f, 1.0f, 1.0f, 1.0f};
    Color secondary{0.55f, 0.60f, 0.70f, 1.0f};
    Color accent{0.30f, 0.60f, 1.0f, 1.0f};
    Color accent_glow{0.18f, 0.76f, 1.0f, 1.0f};
    Color text_main{1.0f, 1.0f, 1.0f, 1.0f};
    Color text_sub{0.55f, 0.60f, 0.70f, 1.0f};
    Color cta_start{0.95f, 0.22f, 0.08f, 1.0f};
    Color cta_end{1.0f, 0.12f, 0.82f, 1.0f};
    Color cta_glow{1.0f, 0.56f, 0.18f, 0.12f};
};

struct Typography {
    f32 hero_size{128.0f};
    f32 hero_tracking{-2.0f};
    Vec2 hero_box{900.0f, 200.0f};
    Vec3 hero_pos{0.0f, -56.0f, 0.0f};

    f32 subtitle_size{52.0f};
    f32 subtitle_tracking{2.0f};
    Vec2 subtitle_box{700.0f, 80.0f};
    Vec3 subtitle_pos{0.0f, 110.0f, 0.0f};

    f32 button_size{40.0f};
    f32 button_tracking{4.0f};
    Vec2 button_box{460.0f, 104.0f};
    Vec3 button_pos{0.0f, 320.0f, 0.0f};

    f32 badge_size{40.0f};
    Vec2 badge_box{88.0f, 88.0f};
    Vec3 badge_pos{0.0f, -370.0f, 0.0f};
};

struct FXConfig {
    f32 glow_strength{1.0f};
    f32 shadow_strength{0.22f};
    f32 blur_strength{1.0f};

    f32 hero_glow_radius{66.0f};
    f32 hero_glow_intensity{1.55f};
    f32 hero_shadow_offset{24.0f};
    f32 hero_shadow_blur{34.0f};

    f32 cta_contact_blur{16.0f};
    f32 cta_ambient_blur{48.0f};
    f32 button_radius{30.0f};

    bool use_gradient_title{true};
    bool use_button_glow{true};
    bool use_background_shapes{true};
    bool show_grid{false};
};

struct StyleConfig {
    std::string name;
    Palette palette;
    Typography typography;
    FXConfig fx;

    // Shortcut to build a shadow style from palette + fx
    [[nodiscard]] ShadowStyle hero_shadow() const {
        return premium::shadow_style(
            palette.accent,
            palette.accent_glow,
            {0.0f, fx.hero_shadow_offset * 0.3f},
            fx.hero_shadow_blur * 0.5f,
            fx.shadow_strength,
            {0.0f, fx.hero_shadow_offset * 1.8f},
            fx.hero_shadow_blur * 3.5f,
            fx.shadow_strength * 0.3f
        );
    }

    [[nodiscard]] Fill hero_gradient() const {
        return Fill::linear(
            {0.0f, 0.0f}, {1.0f, 0.0f}, {
                {0.0f, palette.cta_start},
                {0.50f, Color{
                    (palette.cta_start.r + palette.cta_end.r) * 0.5f,
                    (palette.cta_start.g + palette.cta_end.g) * 0.5f,
                    (palette.cta_start.b + palette.cta_end.b) * 0.5f,
                    1.0f
                }},
                {1.0f, palette.cta_end},
            }
        );
    }

    [[nodiscard]] Fill button_gradient() const {
        return Fill::linear(
            {0.0f, 0.0f}, {1.0f, 0.0f}, {
                {0.0f, palette.cta_start},
                {0.48f, palette.cta_start},
                {1.0f, palette.cta_end},
            }
        );
    }

    [[nodiscard]] Fill bg_gradient_fill() const {
        return Fill::linear(
            {0.0f, 0.0f}, {0.0f, 1.0f}, {
                {0.0f, palette.bg},
                {1.0f, palette.bg_gradient},
            }
        );
    }

    [[nodiscard]] Color stroke_color() const {
        return Color{palette.text_main.r, palette.text_main.g, palette.text_main.b, 0.12f};
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// 3 CANONICAL PALETTES
// ═══════════════════════════════════════════════════════════════════════════════

inline StyleConfig dark_saas_neon() {
    StyleConfig s;
    s.name = "DarkSaaSNeon";
    s.palette = {
        .bg          = {0.01f, 0.06f, 0.24f, 1.0f},
        .bg_gradient = {0.00f, 0.06f, 0.20f, 1.0f},
        .primary     = {1.0f, 1.0f, 1.0f, 1.0f},
        .secondary   = {0.60f, 0.82f, 1.0f, 0.85f},
        .accent      = {0.22f, 0.56f, 1.0f, 1.0f},
        .accent_glow = {0.18f, 0.76f, 1.0f, 1.0f},
        .text_main   = {1.0f, 1.0f, 1.0f, 1.0f},
        .text_sub    = {0.82f, 0.90f, 1.0f, 0.85f},
        .cta_start   = {1.0f, 0.72f, 0.18f, 1.0f},
        .cta_end     = {1.0f, 0.22f, 0.60f, 1.0f},
        .cta_glow    = {1.0f, 0.56f, 0.18f, 0.12f},
    };
    s.typography = {
        .hero_size = 128.0f, .hero_tracking = -2.0f, .hero_box = {900.0f, 200.0f}, .hero_pos = {0.0f, -56.0f, 0.0f},
        .subtitle_size = 26.0f, .subtitle_tracking = 4.0f, .subtitle_box = {800.0f, 60.0f}, .subtitle_pos = {0.0f, 130.0f, 0.0f},
        .button_size = 40.0f, .button_tracking = 4.0f, .button_box = {460.0f, 104.0f}, .button_pos = {0.0f, 320.0f, 0.0f},
    };
    s.fx = {
        .hero_glow_radius = 66.0f, .hero_glow_intensity = 1.55f,
        .hero_shadow_offset = 24.0f, .hero_shadow_blur = 34.0f,
        .use_gradient_title = true, .use_button_glow = true, .use_background_shapes = true,
    };
    return s;
}

inline StyleConfig pastel_masterclass() {
    StyleConfig s;
    s.name = "PastelMasterclass";
    s.palette = {
        .bg          = {1.0f, 1.0f, 1.0f, 1.0f},
        .bg_gradient = {0.98f, 0.96f, 1.0f, 1.0f},
        .primary     = {0.95f, 0.22f, 0.08f, 1.0f},
        .secondary   = {0.38f, 0.40f, 0.46f, 0.85f},
        .accent      = {0.80f, 0.55f, 0.85f, 1.0f},
        .accent_glow = {0.92f, 0.70f, 0.90f, 1.0f},
        .text_main   = {1.0f, 0.58f, 0.30f, 1.0f},
        .text_sub    = {0.38f, 0.40f, 0.46f, 0.85f},
        .cta_start   = {1.0f, 0.72f, 0.18f, 1.0f},
        .cta_end     = {1.0f, 0.22f, 0.60f, 1.0f},
        .cta_glow    = {1.0f, 0.56f, 0.18f, 0.12f},
    };
    s.typography = {
        .hero_size = 128.0f, .hero_tracking = -2.0f, .hero_box = {900.0f, 200.0f}, .hero_pos = {0.0f, -56.0f, 0.0f},
        .subtitle_size = 52.0f, .subtitle_tracking = 2.0f, .subtitle_box = {700.0f, 80.0f}, .subtitle_pos = {0.0f, 110.0f, 0.0f},
        .button_size = 40.0f, .button_tracking = 4.0f, .button_box = {460.0f, 104.0f}, .button_pos = {0.0f, 320.0f, 0.0f},
    };
    s.fx = {
        .hero_glow_radius = 20.0f, .hero_glow_intensity = 0.55f,
        .hero_shadow_offset = 16.0f, .hero_shadow_blur = 20.0f,
        .use_gradient_title = true, .use_button_glow = true, .use_background_shapes = true,
    };
    return s;
}

inline StyleConfig pink_explainer() {
    StyleConfig s;
    s.name = "PinkExplainer";
    s.palette = {
        .bg          = {0.995f, 0.993f, 0.998f, 1.0f},
        .bg_gradient = {0.99f, 0.96f, 1.0f, 1.0f},
        .primary     = {1.0f, 0.30f, 0.72f, 1.0f},
        .secondary   = {0.96f, 0.50f, 0.80f, 1.0f},
        .accent      = {0.90f, 0.52f, 0.76f, 1.0f},
        .accent_glow = {0.96f, 0.74f, 0.90f, 1.0f},
        .text_main   = {1.0f, 0.30f, 0.72f, 1.0f},
        .text_sub    = {0.42f, 0.44f, 0.52f, 1.0f},
        .cta_start   = {1.0f, 0.34f, 0.70f, 0.92f},
        .cta_end     = {0.95f, 0.48f, 0.96f, 1.0f},
        .cta_glow    = {1.0f, 0.45f, 0.70f, 0.08f},
    };
    s.typography = {
        .hero_size = 96.0f, .hero_tracking = -2.0f, .hero_box = {1200.0f, 140.0f}, .hero_pos = {0.0f, 0.0f, 0.0f},
        .subtitle_size = 36.0f, .subtitle_tracking = 0.4f, .subtitle_box = {330.0f, 60.0f}, .subtitle_pos = {0.0f, 0.0f, 0.0f},
        .button_size = 22.0f, .button_tracking = 0.0f, .button_box = {246.0f, 56.0f}, .button_pos = {520.0f, 48.0f, 0.0f},
        .badge_size = 40.0f, .badge_box = {88.0f, 88.0f}, .badge_pos = {0.0f, -370.0f, 0.0f},
    };
    s.fx = {
        .hero_glow_radius = 12.0f, .hero_glow_intensity = 0.35f,
        .hero_shadow_offset = 10.0f, .hero_shadow_blur = 20.0f,
        .button_radius = 20.0f,
        .use_gradient_title = true, .use_button_glow = true, .use_background_shapes = true,
    };
    return s;
}

} // namespace chronon3d::content::text::style
