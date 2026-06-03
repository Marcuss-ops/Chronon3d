#pragma once

#include <optional>
#include <chronon3d/layout/design_kit.hpp>
#include "style_config.hpp"

namespace chronon3d::content::text::style {
namespace component {

// ═══════════════════════════════════════════════════════════════════════════════
// COMPONENT LIBRARY — reusable scene building blocks
// ═══════════════════════════════════════════════════════════════════════════════
// Each function takes a SceneBuilder+, StyleConfig+, and text content+,
// then adds the corresponding layers to the scene.
// ═══════════════════════════════════════════════════════════════════════════════

// ── 1. Background ──────────────────────────────────────────────────────────────
inline void add_background(SceneBuilder& s, const StyleConfig& cfg) {
    s.layer("bg", [&](LayerBuilder& l) {
        l.rect("fill", {
            .size = {W, H},
            .color = cfg.palette.bg,
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = cfg.bg_gradient_fill(),
        });
    });
}

// ── 2. Ambient glow / spotlight ────────────────────────────────────────────────
struct AmbientGlowParams {
    Vec3 pos{0.0f, -420.0f, 0.0f};
    f32 blob_radius{420.0f};
    Color color{0.12f, 0.76f, 1.0f, 0.14f};
    f32 blur_radius{240.0f};
    f32 opacity{0.72f};
    f32 orb_radius{300.0f};
    Color orb_color{0.18f, 0.80f, 1.0f, 0.22f};
    f32 orb_glow{160.0f};
};

inline void add_ambient_lighting(SceneBuilder& s, const StyleConfig& cfg) {
    AmbientGlowParams p;
    p.color     = Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.14f};
    p.orb_color = Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.22f};

    premium::add_ambient_blob(s, "ambient_glow", p.pos, p.blob_radius, p.color, p.blur_radius, p.opacity);
    premium::add_soft_orb(s, "soft_orb", p.pos + Vec3{0.0f, 40.0f, 0.0f}, p.orb_radius, p.orb_color, p.orb_glow);
}

// ── 3. Corner decorative shapes ────────────────────────────────────────────────
inline void add_corner_shapes(SceneBuilder& s, const StyleConfig& cfg) {
    // Top-left wedge
    s.layer("corner_tl", [&](LayerBuilder& l) {
        l.rotate({0.0f, 0.0f, -46.0f});
        l.rect("wedge", {
            .size = {880.0f, 150.0f},
            .color = Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.45f},
            .pos = {-720.0f, -490.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.40f}},
                    {1.0f, Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.35f}},
                }
            ),
        });
    });

    // Bottom-left wedge
    s.layer("corner_bl", [&](LayerBuilder& l) {
        l.rotate({0.0f, 0.0f, -26.0f});
        l.rect("wedge", {
            .size = {1080.0f, 180.0f},
            .color = Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.34f},
            .pos = {-700.0f, 470.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.35f}},
                    {1.0f, Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.30f}},
                }
            ),
        });
    });

    // Bottom-right decorative circle
    s.layer("corner_circle", [&](LayerBuilder& l) {
        l.opacity(0.22f);
        l.circle("orb", {
            .radius = 215.0f,
            .color = Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.18f},
            .pos = {780.0f, 380.0f, 0.0f},
        });
    });
}

// ── 4. Hero title ─────────────────────────────────────────────────────────────
inline void add_hero_title(SceneBuilder& s, const StyleConfig& cfg,
                           const RichTextLine& line,
                           std::optional<Vec3> pos_override = std::nullopt,
                           std::optional<Vec2> box_override = std::nullopt)
{
    const Vec3 pos = pos_override.value_or(cfg.typography.hero_pos);
    [[maybe_unused]] const Vec2 box = box_override.value_or(cfg.typography.hero_box);

    s.layer("hero", [&](LayerBuilder& l) {
        l.position({0.0f, -8.0f, 0.0f});
        l.glow(cfg.fx.hero_glow_radius, cfg.fx.hero_glow_intensity, cfg.palette.accent_glow);
        l.drop_shadow(
            {0.0f, cfg.fx.hero_shadow_offset},
            Color{0.0f, 0.0f, 0.0f, cfg.fx.shadow_strength * 0.7f},
            cfg.fx.hero_shadow_blur
        );
        RichTextLine rich = line;
        if (rich.runs().empty()) {
            rich.run("", cfg.palette.text_main, cfg.typography.hero_size, "assets/fonts/Inter-Bold.ttf");
        }
        rich.size(cfg.typography.hero_size)
            .tracking(cfg.typography.hero_tracking)
            .paint(TextPaint{
                .fill = cfg.palette.text_main,
                .fill_style = cfg.hero_gradient(),
                .stroke_enabled = true,
                .stroke_color = cfg.stroke_color(),
                .stroke_width = 1.5f,
            });

        draw_rich_text(
            l,
            rich,
            pos,
            {
                .anchor = RichTextAnchor::Center,
                .vertical_anchor = RichTextVerticalAnchor::Middle,
                .glyph_padding = 4.0f,
                .snap_to_pixels = true,
                .max_width = box.x,
                .fit_to_width = true,
            }
        );
    });
}

inline void add_hero_title(SceneBuilder& s, const StyleConfig& cfg,
                           const std::string& text,
                           std::optional<Vec3> pos_override = std::nullopt,
                           std::optional<Vec2> box_override = std::nullopt)
{
    RichTextLine line;
    line.run(text, cfg.palette.text_main, cfg.typography.hero_size, "assets/fonts/Inter-Bold.ttf");
    add_hero_title(s, cfg, line, pos_override, box_override);
}

// ── 5. Subtitle ───────────────────────────────────────────────────────────────
struct SubtitleParams {
    Vec3 pos_override{};
    Vec2 box_override{};
    bool use_override{false};
};

inline void add_subtitle(SceneBuilder& s, const StyleConfig& cfg,
                         const RichTextLine& line,
                         std::optional<Vec3> pos_override = std::nullopt,
                         std::optional<Vec2> box_override = std::nullopt)
{
    const Vec3 pos = pos_override.value_or(cfg.typography.subtitle_pos);
    [[maybe_unused]] const Vec2 box = box_override.value_or(cfg.typography.subtitle_box);

    s.layer("subtitle", [&](LayerBuilder& l) {
        l.position({0.0f, -8.0f, 0.0f});
        RichTextLine rich = line;
        if (rich.runs().empty()) {
            rich.run("", cfg.palette.text_sub, cfg.typography.subtitle_size, "assets/fonts/Inter-Regular.ttf");
        }
        rich.size(cfg.typography.subtitle_size)
            .tracking(cfg.typography.subtitle_tracking)
            .paint(TextPaint{
                .fill = cfg.palette.text_sub,
            });
        draw_rich_text(
            l,
            rich,
            pos,
            {
                .anchor = RichTextAnchor::Center,
                .vertical_anchor = RichTextVerticalAnchor::Middle,
                .glyph_padding = 2.0f,
                .snap_to_pixels = true,
                .max_width = box.x,
                .fit_to_width = true,
            }
        );
    });
}

inline void add_subtitle(SceneBuilder& s, const StyleConfig& cfg,
                         const std::string& text,
                         std::optional<Vec3> pos_override = std::nullopt,
                         std::optional<Vec2> box_override = std::nullopt)
{
    RichTextLine line;
    line.run(text, cfg.palette.text_sub, cfg.typography.subtitle_size, "assets/fonts/Inter-Regular.ttf");
    add_subtitle(s, cfg, line, pos_override, box_override);
}

// ── 6. Gradient CTA button ────────────────────────────────────────────────────
inline void add_gradient_button(SceneBuilder& s, const StyleConfig& cfg,
                                const RichTextLine& line,
                                std::optional<Vec3> pos_override = std::nullopt,
                                std::optional<Vec2> box_override = std::nullopt)
{
    const Vec3 pos  = pos_override.value_or(cfg.typography.button_pos);
    [[maybe_unused]] const Vec2 box  = box_override.value_or(cfg.typography.button_box);

    s.layer("cta", [&](LayerBuilder& l) {
        l.position(pos);
        // Contact shadow
        l.drop_shadow(
            {0.0f, cfg.fx.hero_shadow_offset * 0.35f},
            Color{cfg.palette.cta_start.r, cfg.palette.cta_start.g, cfg.palette.cta_start.b, cfg.fx.shadow_strength},
            cfg.fx.cta_contact_blur
        );
        // Ambient glow
        if (cfg.fx.use_button_glow) {
            l.drop_shadow(
                {0.0f, cfg.fx.hero_shadow_offset * 1.1f},
                cfg.palette.cta_glow,
                cfg.fx.cta_ambient_blur
            );
        }
        // Pill background
        l.rounded_rect("pill", {
            .size = box,
            .radius = cfg.fx.button_radius,
            .color = cfg.palette.cta_start,
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = cfg.button_gradient(),
        });
        RichTextLine rich = line;
        if (rich.runs().empty()) {
            rich.run("", {1.0f, 1.0f, 1.0f, 1.0f}, cfg.typography.button_size, "assets/fonts/Inter-Bold.ttf");
        }
        rich.size(cfg.typography.button_size)
            .tracking(cfg.typography.button_tracking)
            .paint(TextPaint{
                .fill = {1.0f, 1.0f, 1.0f, 1.0f},
            });
        draw_rich_text(
            l,
            rich,
            pos,
            {
                .anchor = RichTextAnchor::Center,
                .vertical_anchor = RichTextVerticalAnchor::Middle,
                .glyph_padding = 2.0f,
                .snap_to_pixels = true,
                .max_width = box.x,
                .fit_to_width = true,
            }
        );
    });
}

inline void add_gradient_button(SceneBuilder& s, const StyleConfig& cfg,
                                const std::string& text,
                                std::optional<Vec3> pos_override = std::nullopt,
                                std::optional<Vec2> box_override = std::nullopt)
{
    RichTextLine line;
    line.run(text, {1.0f, 1.0f, 1.0f, 1.0f}, cfg.typography.button_size, "assets/fonts/Inter-Bold.ttf");
    add_gradient_button(s, cfg, line, pos_override, box_override);
}

// ── 7. App badge (Ae-style) ───────────────────────────────────────────────────
inline void add_app_badge(SceneBuilder& s, const StyleConfig& cfg,
                          const std::string& badge_text = "Ae",
                          std::optional<Vec3> pos_override = std::nullopt)
{
    const Vec3 pos = pos_override.value_or(cfg.typography.badge_pos);
    const Vec2 box = cfg.typography.badge_box;

    s.layer("badge", [&](LayerBuilder& l) {
        l.position(pos);
        l.rounded_rect("badge_bg", {
            .size = box,
            .radius = box.x * 0.23f,
            .color = {0.09f, 0.09f, 0.28f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f}, {0.0f, 1.0f}, {
                    {0.0f, Color{0.08f, 0.08f, 0.30f, 1.0f}},
                    {1.0f, Color{0.05f, 0.05f, 0.16f, 1.0f}},
                }
            ),
        });
        // Badge glow dot
        l.circle("dot", {
            .radius = box.x * 0.38f,
            .color = Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.10f},
            .pos = {box.x * 0.4f, -box.y * 0.3f, 0.0f},
        });
        l.text("label", {
            .text = badge_text,
            .size = box,
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 900,
            .font_style = "normal",
            .font_size = cfg.typography.badge_size,
            .color = Color{0.74f, 0.72f, 1.0f, 1.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .shadows = {
                premium::shadow({0.0f, 0.0f}, 10.0f, 0.55f, Color{0.12f, 0.10f, 0.45f, 1.0f}),
            },
        });
    });
}

// ── 8. Decorative sparkle dots ───────────────────────────────────────────────
inline void add_sparkles(SceneBuilder& s, const StyleConfig& cfg,
                         f32 radius = 16.0f, f32 spread_x = 720.0f)
{
    s.layer("sparkles", [&](LayerBuilder& l) {
        l.position({0.0f, 0.0f, 0.0f});
        l.circle("left", {
            .radius = radius,
            .color = Color{cfg.palette.text_main.r, cfg.palette.text_main.g, cfg.palette.text_main.b, 1.0f},
            .pos = {-spread_x, -40.0f, 0.0f},
        });
        l.circle("right", {
            .radius = radius,
            .color = Color{cfg.palette.text_main.r, cfg.palette.text_main.g, cfg.palette.text_main.b, 1.0f},
            .pos = {spread_x, -40.0f, 0.0f},
        });
        // Soft glow on both
        l.glow(22.0f, 0.70f, cfg.palette.accent_glow);
    });
}

// ── 9. Floor arc / luminous bar ──────────────────────────────────────────────
inline void add_floor_arc(SceneBuilder& s, const StyleConfig& cfg) {
    s.layer("floor_arc", [&](LayerBuilder& l) {
        l.position({0.0f, 378.0f, 0.0f});
        l.glow(20.0f, 1.0f, cfg.palette.accent_glow);
        l.rect("bar", {
            .size = {1380.0f, 12.0f},
            .color = Color{cfg.palette.accent_glow.r, cfg.palette.accent_glow.g, cfg.palette.accent_glow.b, 0.82f},
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.0f}},
                    {0.5f, cfg.palette.accent_glow},
                    {1.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.0f}},
                }
            ),
        });
    });
}

// ── 10. Ambient arc (rounded rect glow bar) ──────────────────────────────────
inline void add_ambient_arc(SceneBuilder& s, const StyleConfig& cfg) {
    s.layer("arc", [&](LayerBuilder& l) {
        l.position({0.0f, 320.0f, 0.0f});
        l.glow(34.0f, 1.10f, cfg.palette.accent_glow);
        l.rounded_rect("curve", {
            .size = {1380.0f, 230.0f},
            .radius = 118.0f,
            .color = Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.14f},
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.08f}},
                    {0.52f, cfg.palette.accent_glow},
                    {1.0f, Color{cfg.palette.accent.r, cfg.palette.accent.g, cfg.palette.accent.b, 0.10f}},
                }
            ),
        });
    });
}

} // namespace component
} // namespace chronon3d::content::text::style
