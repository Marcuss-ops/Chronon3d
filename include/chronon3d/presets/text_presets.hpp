#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_presets.hpp — F3.C — 5 reusable TextDefinition presets
//
// Ready-to-use authoring presets that produce canonical TextDefinition DTOs.
// Each preset returns a fully-configured TextDefinition with sensible
// defaults for the target use case.  All presets use the canonical
// TextDefinition → LayerBuilder::text() pipeline (F2.C).
//
// Usage:
//   #include <chronon3d/presets/text_presets.hpp>
//
//   const auto canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);
//   l.text("hero", presets::title_preset("WELCOME TO CHRONON3D", canvas));
//   l.text("sub",  presets::subtitle_preset("Motion Graphics Engine", canvas));
//   l.text("cap",  presets::caption_preset("Frame 42 — 2026", canvas));
//   l.text("hero", presets::kinetic_hero_preset("UNLEASH CREATIVITY", canvas));
//   l.text("low3", presets::lower_third_preset("John Doe • Creative Director", canvas));
//
// Golden tests: tests/text_golden/presets/test_text_presets_golden.cpp
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/preset_constraints.hpp>  // TextBoxConstraints, AspectRatioPolicy, resolve_text_box_constraints
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>  // CanvasInfo
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <string>

namespace chronon3d::presets {

// Shared helpers imported from preset_constraints.hpp (via text namespace):
using text::TextBoxConstraints;
using text::AspectRatioPolicy;
using text::resolve_text_box_constraints;

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_preset — large bold centered title
// ═══════════════════════════════════════════════════════════════════════════
//
// Target: hero titles, section headers, opening cards.
// Font: Inter Bold, 96px, white with subtle drop shadow.
// Layout: centered, full-width box, PixelInk centering.

[[nodiscard]] inline TextDefinition title_preset(
    std::string text,
    const CanvasInfo& canvas,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1920.0f / 1920.0f,
        .height_fraction = 200.0f / 1080.0f,
        .min_width       = 640.0f,
        .min_height      = 64.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept {
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
    def.frame.size           = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.placement      = TextPlacement{TextPlacementKind::CanvasCenter};
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

[[nodiscard]] inline TextDefinition subtitle_preset(
    std::string text,
    const CanvasInfo& canvas,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1200.0f / 1920.0f,
        .height_fraction = 80.0f / 1080.0f,
        .min_width       = 400.0f,
        .min_height      = 32.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept {
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
    def.frame.size           = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.placement      = TextPlacement{TextPlacementKind::SafeAreaBottom};
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
// Layout: bottom-center, full-width box.

[[nodiscard]] inline TextDefinition caption_preset(
    std::string text,
    const CanvasInfo& canvas,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1920.0f / 1920.0f,
        .height_fraction = 40.0f / 1080.0f,
        .min_width       = 640.0f,
        .min_height      = 16.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept {
    TextDefinition def;
    def.content.value       = std::move(text);
    def.style.font          = FontSpec{
        .font_family = "Inter",
        .font_weight = 400,
        .font_size   = 22.0f,
    };
    def.style.color         = Color{1.0f, 1.0f, 1.0f, 0.65f};
    def.frame.size           = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.placement      = TextPlacement{TextPlacementKind::BottomCenter};
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
// Layout: centered, full-width box, tight line-height for multi-line.

[[nodiscard]] inline TextDefinition kinetic_hero_preset(
    std::string text,
    const CanvasInfo& canvas,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1800.0f / 1920.0f,
        .height_fraction = 280.0f / 1080.0f,
        .min_width       = 600.0f,
        .min_height      = 96.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept {
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
    def.frame.size           = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.placement      = TextPlacement{TextPlacementKind::CanvasCenter};
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

[[nodiscard]] inline TextDefinition lower_third_preset(
    std::string text,
    const CanvasInfo& canvas,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 800.0f / 1920.0f,
        .height_fraction = 60.0f / 1080.0f,
        .min_width       = 320.0f,
        .min_height      = 24.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept {
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
    def.frame.size           = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.placement      = TextPlacement{TextPlacementKind::BottomLeft};
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

// ═══════════════════════════════════════════════════════════════════════════
// Deprecated overloads — migration shims for the old hard-coded 1920×1080 API.
// These overloads are provided temporarily so existing callers compile while
// they migrate to the responsive CanvasInfo-based API.  They will be removed
// once all callers have been updated.
// ═══════════════════════════════════════════════════════════════════════════

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1.")]]
[[nodiscard]] inline TextDefinition title_preset(std::string text) noexcept {
    return title_preset(std::move(text), CanvasInfo::from_dimensions(1920.0f, 1080.0f));
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1.")]]
[[nodiscard]] inline TextDefinition subtitle_preset(std::string text) noexcept {
    return subtitle_preset(std::move(text), CanvasInfo::from_dimensions(1920.0f, 1080.0f));
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1.")]]
[[nodiscard]] inline TextDefinition caption_preset(std::string text) noexcept {
    return caption_preset(std::move(text), CanvasInfo::from_dimensions(1920.0f, 1080.0f));
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1.")]]
[[nodiscard]] inline TextDefinition kinetic_hero_preset(std::string text) noexcept {
    return kinetic_hero_preset(std::move(text), CanvasInfo::from_dimensions(1920.0f, 1080.0f));
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1.")]]
[[nodiscard]] inline TextDefinition lower_third_preset(std::string text) noexcept {
    return lower_third_preset(std::move(text), CanvasInfo::from_dimensions(1920.0f, 1080.0f));
}

} // namespace chronon3d::presets
