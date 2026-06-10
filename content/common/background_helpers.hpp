#pragma once

// ── Shared Background Helpers ────────────────────────────────────────────────
//
// Unified `add_common_background` helper used by ALL composition families
// (Minimalist, Animation, TextTypewriter) so the grid background layer is
// consistent across the codebase.  Each call site picks a BackgroundStyle
// from the BackgroundStyles namespace.
//
// Example:
//
//   #include "content/common/background_helpers.hpp"
//
//   SceneBuilder s(ctx);
//   add_common_background(s, BackgroundStyles::Minimalist());
//   // ... add text/animation layers on top
//
// Implementation notes:
//   - Uses two separate layers (solid bg fill + centered grid) for clarity
//     and to avoid grid_background's internal fill overwriting the bg color.
//   - Both layers are cache_static + pin_to(Anchor::Center) + centered grid.
//   - Style presets encapsulate the visual identity of each family.

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d::content::backgrounds {

// ── BackgroundStyle ──────────────────────────────────────────────────────────
// All parameters needed to draw a bg+grid background.  Defaults match the
// Minimalist family (dark gray bg, subtle white grid).
struct BackgroundStyle {
    Color bg_color{0.025f, 0.027f, 0.031f, 1.0f};
    Color grid_color{0.58f, 0.61f, 0.66f, 0.045f};
    f32   spacing{136.0f};
    f32   minor_thickness{1.0f};
    f32   major_thickness{2.0f};
    i32   major_every{4};
    Vec2  size{1920.0f, 1080.0f};
};

// ── Predefined styles ───────────────────────────────────────────────────────
// One per "family" so call sites read like `BackgroundStyles::Minimalist()`
// or `BackgroundStyles::TextTypewriter()` — no magic numbers at the call site.
namespace BackgroundStyles {

// Minimalist: dark gray bg, subtle near-white grid (wider spacing).
[[nodiscard]] inline BackgroundStyle Minimalist() {
    return BackgroundStyle{
        .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
        .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
        .spacing = 136.0f,
        .minor_thickness = 1.0f,
        .major_thickness = 2.0f,
        .major_every = 4,
    };
}

// TextTypewriter: deep navy bg, blue grid (denser spacing).
[[nodiscard]] inline BackgroundStyle TextTypewriter() {
    return BackgroundStyle{
        .bg_color = {0.01f, 0.012f, 0.022f, 1.0f},
        .grid_color = {0.18f, 0.5f, 0.96f, 0.12f},
        .spacing = 96.0f,
        .minor_thickness = 1.0f,
        .major_thickness = 2.5f,
        .major_every = 4,
    };
}

} // namespace BackgroundStyles

// ── add_common_background ───────────────────────────────────────────────────
// Adds two layers to the scene: a solid bg fill and a centered grid on top.
// Both are cache_static and pin_to(Anchor::Center) so the grid stays locked
// to the canvas center even if a camera is in motion.
inline void add_common_background(SceneBuilder& s, const BackgroundStyle& style = BackgroundStyles::Minimalist()) {
    // Solid bg fill
    s.layer("background_fill", [style](LayerBuilder& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.rect("fill", {
            .size = style.size,
            .color = style.bg_color,
        });
    });
    // Centered grid (transparent bg so the fill layer shows through)
    s.layer("background_grid", [style](LayerBuilder& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = style.size,
            .offset = {0.0f, 0.0f},
            .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},   // transparent — let fill show
            .grid_color = style.grid_color,
            .spacing = style.spacing,
            .minor_thickness = style.minor_thickness,
            .major_thickness = style.major_thickness,
            .major_every = style.major_every,
            .centered = true,
        });
    });
}

} // namespace chronon3d::content::backgrounds
