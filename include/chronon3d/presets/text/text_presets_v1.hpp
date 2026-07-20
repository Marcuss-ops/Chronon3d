#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_presets_v1.hpp — M1.8 §3C / TICKET-SIMPLICITY-PRESETS
//
// 5 reusable authoring presets that produce ONLY `TextDefinition` (the
// canonical authoring DTO from F2.A).  No cache, no resolver, no registry,
// no service-locator.  The presets compose with `LayerBuilder::text(name,
// TextDefinition)` (F2.C) and the `Layer::text(name).content()/.place()`
// ergonomic chain (M1.8 §2B/§5).
//
// ── M1.8 §5A / TICKET-SIMPLICITY-DEPRECATION NOTICE ──
// This header is the CANONICAL REPLACEMENT for the deprecated
// `content::text::centered_text()` + `content::text::glow_text()` helpers.
// Per the §5A atomic commit, those two helpers now carry compile-time
// `[[deprecated]]` attributes and emit one-shot `spdlog::warn` calls
// pointing to this header as the migration target.
//
//   `centered_text(text)`          → `presets::text::title_centered(text, canvas)`
//   `glow_text(text, glow_color)`  → `presets::text::kinetic_word(text, canvas)` +
//                                    `def.effects.glow = GlowParams{...}`
//                                    overlay
//
// See `TICKET-SIMPLICITY-DEPRECATION` for the 5-step migration order
// (nuova API → migrazione → adapter → warning → rimozione).  The §5A
// commit lands steps 1+4 (canonical surface + gate warning); steps 2, 3,
// 5 are forward-pointed to subsequent M1.8 commits.
//
// ANTI-DUPLICATION HONOUR (AGENTS.md §anti-duplication rules):
//   - Reuses `TextContent` (builder_params.hpp canonical).
//   - Reuses `TextDefStyle` + `TextFrame` + `TextPlacement` (all canonical).
//   - Reuses the same `from_text_spec` adapter chain as the existing
//     `centered_text()` / `glow_text()` helpers (so the preset pipeline
//     is the SAME pipeline as the helper pipeline).
//   - Does NOT introduce `PresetCache` / `PresetRegistry` / `PresetResolver`
//     — these are forbidden by AGENTS.md anti-duplication (would be a
//     new singleton/registry/resolver per the §3A "no new cache/registry"
//     pattern preserved by this header).
//   - `max_width` for `title_centered()` is a std::optional<px> forwarded
//     to the layout.box; no auto-fit / shrink-to-fit math at this layer
//     (auto-fit is a separate forward-point, TICKET-FASE4-AUTOFIT).
//
// LIFECYCLE:
//   - F2.A baseline:  TextDefinition is the SOLE canonical authoring DTO
//                     (F2.A commit, 2026-07-10).
//   - F2.C:           text() / text_run() / centered_text() / glow_text()
//                     all return TextDefinition via from_text_spec().
//   - §3C (this):     5 reusable preset functions returning TextDefinition.
//                     Companion: test_presets_stability.cpp (15 assertions
//                     = 5 presets × 3 invariants per preset).
//   - FU10 forward:   per-preset golden frame PNGs in
//                     test_renders/golden/text/presets/<name>.png (5 PNGs).
//   - Phase A.3:      TextEffects::glow consolidation per-preset (gated on
//                     F3.B/C TextEffects field population; not in §3C).
//
// CALL-SITE GUIDANCE (M1.8 §2B "centra un titolo in < 10 righe" criterion):
//   ```cpp
//   LayerBuilder lb("my_comp", SampleTime{});
//   lb.screen_dimensions(1920.0f, 1080.0f);
//   const auto canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);
//   authoring::Layer lyr(lb, canvas);
//   lyr.text("title").content("CHRONON3D").font("Inter-Bold.ttf", 96.0f)
//                   .place(presets::text::title_centered("CHRONON3D", canvas).frame.placement,
//                          TextAnchor::Center);
//   // — OR — using the preset directly:
//   lyr.text("title").content(presets::text::title_centered("CHRONON3D", canvas));
//   ```
//   The canonical centered-title chain (with the preset feeding
//   `content()`) is 4 method calls: satisfies the §2B criterion.
//
// PRESET CATALOG (5):
//   1. title_centered(text, canvas, font_size, constraints, policy)
//        — Canvas center, 96pt
//   2. subtitle_bottom(text, canvas, font_size, constraints, policy)
//        — SafeAreaBottom, 48pt
//   3. caption_safe_area(text, canvas, font_size, constraints, policy)
//        — SafeAreaCenter, 36pt
//   4. kinetic_word(text, canvas, font_size, accent_color, constraints, policy)
//        — Canvas center, 120pt
//   5. lower_third(text, canvas, font_size, constraints, policy)
//        — BottomLeft, 42pt
//
// Each preset sets:
//   - content.value = the user-provided text
//   - style.font.font_size / font_family / font_weight (Poppins defaults
//     to match the existing `centered_text()` helper in
//     content/text/text_helpers_centered.hpp)
//   - style.color = white (or accent_color for kinetic_word)
//   - frame.size = layout box size derived from canvas + constraints
//   - frame.anchor / frame.align / frame.vertical_align = Center / Middle
//   - frame.placement = semantic placement (CanvasCenter, SafeAreaBottom,
//     SafeAreaCenter, BottomLeft) — no hard-coded coordinates.
//
// The presets are fully responsive: they accept a `CanvasInfo` describing
// the target canvas (width, height and safe-area margins) and size the
// text box through `TextBoxConstraints` + `AspectRatioPolicy`.  This makes
// the same preset work on 16:9, 9:16, 1:1 and arbitrary resolutions
// without coordinate hard-coding.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/preset_constraints.hpp>  // TextBoxConstraints, AspectRatioPolicy, resolve_text_box_constraints
#include <chronon3d/text/text_definition.hpp>       // TextDefinition, TextContent
#include <chronon3d/text/text_placement.hpp>        // TextPlacement, TextPlacementKind, SafeAreaPreset
#include <chronon3d/text/resolve_text_placement.hpp>  // CanvasInfo
#include <chronon3d/scene/builders/builder_params.hpp>  // TextContent, FontSpec
#include <chronon3d/math/glm_types.hpp>             // Vec2, Vec3
#include <chronon3d/math/color.hpp>                  // Color

#include <optional>
#include <string>
#include <utility>

namespace chronon3d::presets::text {

// Shared helpers imported from preset_constraints.hpp:
//   TextBoxConstraints, AspectRatioPolicy, resolve_text_box_constraints()

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered
// ═══════════════════════════════════════════════════════════════════════════

/// Canonical hero/centered title preset.  Places the text at the canvas
/// center, Poppins-Bold 96pt (matches the `centered_text()` helper default).
///
/// Parameters:
///   text       — the string to render
///   canvas     — target canvas (width, height, safe-area margins)
///   font_size  — target font size in canvas pixels (default 96.0f)
///   constraints — optional box sizing constraints
///   policy     — aspect-ratio scaling policy
///
/// Placement: CanvasCenter + anchor Center.
[[nodiscard]] inline TextDefinition title_centered(
    std::string text,
    const CanvasInfo& canvas,
    float font_size = 96.0f,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 900.0f / 1920.0f,
        .height_fraction = 160.0f / 1080.0f,
        .min_width       = 320.0f,
        .min_height      = 48.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 700;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};

    def.frame.size            = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 0.95f;
    // Semantic placement: canvas center, no hard-coded coordinates.
    def.frame.placement = TextPlacement{TextPlacementKind::CanvasCenter};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom
// ═══════════════════════════════════════════════════════════════════════════

/// Subtitle at the bottom safe area.  Poppins-SemiBold 48pt, white on
/// 5% safe-margin black background (sRGB-grey, no fill — visual
/// sophistication deferred to user CSS).
///
/// Placement: SafeAreaBottom + anchor Center.
[[nodiscard]] inline TextDefinition subtitle_bottom(
    std::string text,
    const CanvasInfo& canvas,
    float font_size = 48.0f,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1200.0f / 1920.0f,
        .height_fraction = 100.0f / 1080.0f,
        .min_width       = 240.0f,
        .min_height      = 32.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Regular.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 600;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{0.9f, 0.9f, 0.9f, 1.0f};

    def.frame.size            = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.05f;
    // Semantic placement: bottom-center of safe area.
    def.frame.placement = TextPlacement{TextPlacementKind::SafeAreaBottom};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area
// ═══════════════════════════════════════════════════════════════════════════

/// Centered caption inside the safe area bounds.  Poppins-Medium 36pt
/// (smaller than subtitle to leave breathing room; the 5% safe-area
/// margin applies on all 4 sides).
///
/// Placement: SafeAreaCenter + anchor Center.
[[nodiscard]] inline TextDefinition caption_safe_area(
    std::string text,
    const CanvasInfo& canvas,
    float font_size = 36.0f,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1640.0f / 1920.0f,
        .height_fraction = 80.0f / 1080.0f,
        .min_width       = 240.0f,
        .min_height      = 24.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Regular.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 500;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};

    def.frame.size            = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.15f;
    // Semantic placement: center of safe area bounds.
    def.frame.placement = TextPlacement{TextPlacementKind::SafeAreaCenter};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. kinetic_word
// ═══════════════════════════════════════════════════════════════════════════

/// Single-word hero text for kinetic typography.  Poppins-Black 120pt
/// (largest preset, reserved for hero reveal animations).  Accent color
/// is opt-in (default white; e.g. orange/red for "WARN", green for
/// "OK" badge use cases).
///
/// Placement: CanvasCenter + anchor Center.
[[nodiscard]] inline TextDefinition kinetic_word(
    std::string text,
    const CanvasInfo& canvas,
    float font_size = 120.0f,
    std::optional<Color> accent_color = std::nullopt,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1600.0f / 1920.0f,
        .height_fraction = 240.0f / 1080.0f,
        .min_width       = 320.0f,
        .min_height      = 64.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 900;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = accent_color.value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});

    def.frame.size            = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::None;  // single-word
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 0.90f;
    // Semantic placement: canvas center.
    def.frame.placement = TextPlacement{TextPlacementKind::CanvasCenter};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third
// ═══════════════════════════════════════════════════════════════════════════

/// Lower-third broadcast-style name plate.  Poppins-Bold 42pt, left-aligned
/// to the bottom-left safe area, white on dark-grey caption (visual
/// sophistication deferred to user).
///
/// Placement: BottomLeft + anchor TopLeft.
[[nodiscard]] inline TextDefinition lower_third(
    std::string text,
    const CanvasInfo& canvas,
    float font_size = 42.0f,
    const TextBoxConstraints& constraints = TextBoxConstraints{
        .width_fraction  = 1640.0f / 1920.0f,
        .height_fraction = 100.0f / 1080.0f,
        .min_width       = 240.0f,
        .min_height      = 32.0f,
    },
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 700;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};

    def.frame.size            = resolve_text_box_constraints(constraints, canvas, policy);
    def.frame.anchor          = TextAnchor::BottomLeft;  // box sits on bottom-left safe area corner
    def.frame.align           = TextAlign::Left;
    def.frame.vertical_align  = VerticalAlign::Bottom;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.05f;
    // Semantic placement: bottom-left corner of the safe area.
    def.frame.placement = TextPlacement{TextPlacementKind::BottomLeft};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deprecated overloads — migration shims for the old hard-coded 1920×1080 API.
// These overloads are provided temporarily so existing callers compile while
// they migrate to the responsive CanvasInfo-based API.  They will be removed
// once all callers have been updated (TICKET-SIMPLICITY-PRESETS).
// ═══════════════════════════════════════════════════════════════════════════

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1. "
             "See TICKET-SIMPLICITY-PRESETS.")]]
[[nodiscard]] inline TextDefinition title_centered(
    std::string text,
    float font_size,
    std::optional<float> max_width = std::nullopt) noexcept
{
    TextBoxConstraints constraints{
        .width_fraction  = max_width.value_or(900.0f) / 1920.0f,
        .height_fraction = 160.0f / 1080.0f,
        .min_width       = 320.0f,
        .min_height      = 48.0f,
    };
    return title_centered(std::move(text),
                          CanvasInfo::from_dimensions(1920.0f, 1080.0f),
                          font_size,
                          constraints);
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1. "
             "See TICKET-SIMPLICITY-PRESETS.")]]
[[nodiscard]] inline TextDefinition subtitle_bottom(
    std::string text,
    float font_size = 48.0f) noexcept
{
    return subtitle_bottom(std::move(text),
                           CanvasInfo::from_dimensions(1920.0f, 1080.0f),
                           font_size);
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1. "
             "See TICKET-SIMPLICITY-PRESETS.")]]
[[nodiscard]] inline TextDefinition caption_safe_area(
    std::string text,
    float font_size = 36.0f) noexcept
{
    return caption_safe_area(std::move(text),
                             CanvasInfo::from_dimensions(1920.0f, 1080.0f),
                             font_size);
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1. "
             "See TICKET-SIMPLICITY-PRESETS.")]]
[[nodiscard]] inline TextDefinition kinetic_word(
    std::string text,
    float font_size = 120.0f,
    std::optional<Color> accent_color = std::nullopt) noexcept
{
    return kinetic_word(std::move(text),
                        CanvasInfo::from_dimensions(1920.0f, 1080.0f),
                        font_size,
                        accent_color);
}

[[deprecated("Pass an explicit CanvasInfo — hard-coded 1920×1080 default is removed in V1. "
             "See TICKET-SIMPLICITY-PRESETS.")]]
[[nodiscard]] inline TextDefinition lower_third(
    std::string text,
    float font_size = 42.0f) noexcept
{
    return lower_third(std::move(text),
                       CanvasInfo::from_dimensions(1920.0f, 1080.0f),
                       font_size);
}

} // namespace chronon3d::presets::text
