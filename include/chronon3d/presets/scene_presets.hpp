#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/camera/camera_rig.hpp>
#include <chronon3d/scene/camera/camera_rig_animated_presets.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_placement.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/scenes/legacy_text_animator.hpp>

namespace chronon3d::scene_presets {

// ═══════════════════════════════════════════════════════════════════════════════
// Scene Presets — professional pre-composed scenes using the Chronon3D motion
// system, camera rig, stagger, and text animator.
//
// Each preset returns a Composition that can be evaluated at any frame.
//
// Usage:
//   auto comp = scene_presets::saas_intro();
//   Scene scene = comp.evaluate(Frame{45});
// ═══════════════════════════════════════════════════════════════════════════════

constexpr f32 W  = 1920.0f;
constexpr f32 H  = 1080.0f;

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace detail {

inline std::string default_font() { return "assets/fonts/Inter-Bold.ttf"; }

inline FillStyle soft_radial_gradient(Color inner, Color outer) {
    return FillStyle::radial({0.5f, 0.5f}, 0.5f, {
        {graphics::GradientStop{0.0f, inner}},
        {graphics::GradientStop{1.0f, outer}},
    });
}

inline FillStyle linear_gradient_v(Color top, Color bottom) {
    return FillStyle::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {
        {graphics::GradientStop{0.0f, top}},
        {graphics::GradientStop{1.0f, bottom}},
    });
}

inline FillStyle linear_gradient_h(Color left, Color right) {
    return FillStyle::linear({0.0f, 0.0f}, {1.0f, 0.0f}, {
        {graphics::GradientStop{0.0f, left}},
        {graphics::GradientStop{1.0f, right}},
    });
}

// Helper: create TextSpec with minimal boilerplate.
inline TextSpec text_preset(
    std::string text,
    f32 font_size,
    int font_weight,
    Color color,
    TextAlign align,
    Vec2 size = {600.0f, 60.0f},
    Vec3 pos = {0.0f, 0.0f, 0.0f},
    std::string font_path = default_font(),
    std::string font_family = "Inter",
    std::string font_style = "normal"
) {
    TextSpec tp;
    tp.content.value = std::move(text);
    tp.layout.box = size;
    tp.placement = TextPlacement{TextPlacementKind::Absolute, {pos.x, pos.y}};
    tp.font.font_path = std::move(font_path);
    tp.font.font_family = std::move(font_family);
    tp.font.font_weight = font_weight;
    tp.font.font_style = std::move(font_style);
    tp.font.font_size = font_size;
    tp.appearance.color = color;
    tp.layout.align = align;
    tp.layout.vertical_align = VerticalAlign::Middle;
    tp.placement = TextPlacement{TextPlacementKind::Absolute, {pos.x, pos.y}};
    return tp;
}

} // namespace detail

// ── 5 scene presets — extracted into separate headers (FASE 11) ───────
// Each header is self-contained and lives in presets/scenes/.
// Included inline here for backward compatibility.
#include "scenes/saas_intro.hpp"
#include "scenes/floating_cards_hero.hpp"
#include "scenes/kinetic_title.hpp"
#include "scenes/depth_product_reveal.hpp"
#include "scenes/apple_style_hero.hpp"

} // namespace chronon3d::scene_presets
