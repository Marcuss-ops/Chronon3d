#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_presets.hpp — F3.C — 5 reusable TextDefinition presets
//
// Ready-to-use authoring presets that produce canonical TextDefinition DTOs.
// Each preset returns a fully-configured TextDefinition with sensible
// defaults for the target use case.  All presets use the canonical
// TextDefinition → LayerBuilder::text() pipeline (F2.C adapter).
//
// Usage:
//   #include <chronon3d/presets/text_presets.hpp>
//
//   l.text("hero", presets::title_preset("WELCOME TO CHRONON3D"));
//   l.text("sub",  presets::subtitle_preset("Motion Graphics Engine"));
//   l.text("cap",  presets::caption_preset("Frame 42 — 2026"));
//   l.text("hero", presets::kinetic_hero_preset("UNLEASH CREATIVITY"));
//   l.text("low3", presets::lower_third_preset("John Doe • Creative Director"));
//
// Golden tests: tests/text_golden/presets/test_text_presets_golden.cpp
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <string>

namespace chronon3d::presets {

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_preset — large bold centered title
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: hero titles, section headers, opening cards.
// Font: Inter Bold, 96px, white with subtle drop shadow.
// Layout: centered, 1920×200 box, PixelInk centering.

inline TextDefinition title_preset(std::string text) {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 800,
        .font_size   = 96.0f,
    };
    def.style.color         = Color{1.0f, 1.0f, 1.0f, 1.0f};
    def.style.shadows       = {TextShadow{
        .offset = {0.0f, 8.0f},
        .color  = Color{0.0f, 0.0f, 0.0f, 0.4f},
        .radius = 12.0f,
    }};
    def.frame.size           = Vec2{1920.0f, 200.0f};
    def.frame.placement      = TextPlacement{TextPlacementKind::Absolute, {960.0f, 540.0f}};
    def.frame.anchor         = TextAnchor::Center;
    def.frame.align          = TextAlign::Center;
    def.frame.vertical_align = VerticalAlign::Middle;
    def.frame.wrap           = TextWrap::Word;
    def.frame.overflow       = TextOverflow::Clip;
    def.frame.centering_mode = TextCenteringMode::PixelInk;
    def.frame.line_height    = 0.95f;
    def.frame.tracking       = -1.0f;
    def.frame.max_lines      = 2;
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_preset — medium subtitle with dark background bar
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: secondary text below titles, descriptive labels.
// Font: Inter SemiBold, 42px, light gray with dark translucent background.
// Layout: centered, 1200×80 box, 80px below canvas center.

inline TextDefinition subtitle_preset(std::string text) {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 600,
        .font_size   = 42.0f,
    };
    def.style.color         = Color{0.9f, 0.9f, 0.92f, 1.0f};
    def.style.box_style     = TextBoxStyle{
        .enabled    = true,
        .background = Color{0.0f, 0.0f, 0.0f, 0.55f},
        .padding    = {24.0f, 12.0f},
        .radius     = 8.0f,
    };
    def.frame.size           = Vec2{1200.0f, 80.0f};
    def.frame.placement      = TextPlacement{TextPlacementKind::Absolute, {960.0f, 620.0f}};
    def.frame.anchor         = TextAnchor::Center;
    def.frame.align          = TextAlign::Center;
    def.frame.vertical_align = VerticalAlign::Middle;
    def.frame.wrap           = TextWrap::Word;
    def.frame.overflow       = TextOverflow::Clip;
    def.frame.centering_mode = TextCenteringMode::PixelInk;
    def.frame.line_height    = 1.1f;
    def.frame.tracking       = 0.5f;
    def.frame.max_lines      = 1;
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_preset — small informational caption
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: frame counters, timestamps, technical labels, footnotes.
// Font: Inter Regular, 22px, semi-transparent white, wide tracking.
// Layout: bottom-center, 1920×40 box.

inline TextDefinition caption_preset(std::string text) {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 400,
        .font_size   = 22.0f,
    };
    def.style.color         = Color{1.0f, 1.0f, 1.0f, 0.65f};
    def.frame.size           = Vec2{1920.0f, 40.0f};
    def.frame.placement      = TextPlacement{TextPlacementKind::Absolute, {960.0f, 1040.0f}};
    def.frame.anchor         = TextAnchor::Center;
    def.frame.align          = TextAlign::Center;
    def.frame.vertical_align = VerticalAlign::Middle;
    def.frame.wrap           = TextWrap::None;
    def.frame.overflow       = TextOverflow::Ellipsis;
    def.frame.centering_mode = TextCenteringMode::PixelInk;
    def.frame.line_height    = 1.0f;
    def.frame.tracking       = 2.0f;
    def.frame.max_lines      = 1;
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_hero_preset — large animated hero text with premium styling
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: opening sequences, brand reveals, kinetic typography.
// Font: Inter Black, 108px, with gradient, stroke, glow, and shadows.
// Layout: centered, 1800×280 box, tight line-height for multi-line.

inline TextDefinition kinetic_hero_preset(std::string text) {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 900,
        .font_size   = 108.0f,
    };
    def.style.color         = Color{1.0f, 1.0f, 1.0f, 1.0f};
    def.style.paint         = TextPaint{
        .stroke_enabled = true,
        .stroke_color   = Color{0.0f, 0.02f, 0.12f, 0.85f},
        .stroke_width   = 2.5f,
    };
    def.style.shadows       = {
        TextShadow{
            .offset = {0.0f, 12.0f},
            .color  = Color{0.0f, 0.0f, 0.0f, 0.45f},
            .radius = 24.0f,
        },
        TextShadow{
            .offset = {0.0f, 0.0f},
            .color  = Color{0.4f, 0.2f, 1.0f, 0.35f},
            .radius = 18.0f,
        },
    };
    def.frame.size           = Vec2{1800.0f, 280.0f};
    def.frame.placement      = TextPlacement{TextPlacementKind::Absolute, {960.0f, 500.0f}};
    def.frame.anchor         = TextAnchor::Center;
    def.frame.align          = TextAlign::Center;
    def.frame.vertical_align = VerticalAlign::Middle;
    def.frame.wrap           = TextWrap::Word;
    def.frame.overflow       = TextOverflow::Clip;
    def.frame.centering_mode = TextCenteringMode::PixelInk;
    def.frame.line_height    = 0.9f;
    def.frame.tracking       = -1.5f;
    def.frame.max_lines      = 3;
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third_preset — bottom-third overlay for names/titles
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: speaker names, job titles, location labels, broadcast-style L3s.
// Font: Inter Bold, 36px, white text on dark translucent background.
// Layout: left-aligned, bottom-left position with padding.

inline TextDefinition lower_third_preset(std::string text) {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 700,
        .font_size   = 36.0f,
    };
    def.style.color         = Color{1.0f, 1.0f, 1.0f, 1.0f};
    def.style.box_style     = TextBoxStyle{
        .enabled    = true,
        .background = Color{0.0f, 0.0f, 0.0f, 0.7f},
        .padding    = {32.0f, 16.0f},
        .radius     = 6.0f,
    };
    def.frame.size           = Vec2{800.0f, 60.0f};
    def.frame.placement      = TextPlacement{TextPlacementKind::Absolute, {80.0f, 960.0f}};
    def.frame.anchor         = TextAnchor::TopLeft;
    def.frame.align          = TextAlign::Left;
    def.frame.vertical_align = VerticalAlign::Middle;
    def.frame.wrap           = TextWrap::None;
    def.frame.overflow       = TextOverflow::Ellipsis;
    def.frame.centering_mode = TextCenteringMode::LayoutBox;
    def.frame.line_height    = 1.1f;
    def.frame.tracking       = 0.0f;
    def.frame.max_lines      = 1;
    return def;
}

} // namespace chronon3d::presets
