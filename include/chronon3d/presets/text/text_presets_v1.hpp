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
//   `centered_text(text)`          → `presets::text::title_centered(text)`
//   `glow_text(text, glow_color)`  → `presets::text::kinetic_word(text)` +
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
//   authoring::Layer lyr(lb, CanvasInfo::from_dimensions(
//       1920.0f, 1080.0f));
//   lyr.text("title").content("CHRONON3D").font("Inter-Bold.ttf", 96.0f)
//                   .place(presets::text::title_centered("CHRONON3D").frame.placement,
//                          TextAnchor::Center);
//   // — OR — using the preset directly:
//   lyr.text("title").content(presets::text::title_centered("CHRONON3D"));
//   ```
//   The canonical centered-title chain (with the preset feeding
//   `content()`) is 4 method calls: satisfies the §2B criterion.
//
// PRESET CATALOG (5):
//   1. title_centered(text, font_size, max_width)       — Canvas center, 96pt
//   2. subtitle_bottom(text, font_size)                 — SafeAreaBottom, 48pt
//   3. caption_safe_area(text, font_size)                — SafeAreaCenter, 36pt
//   4. kinetic_word(text, font_size, accent_color)       — Canvas center, 120pt
//   5. lower_third(text, font_size)                      — BottomLeft, 42pt
//
// Each preset sets:
//   - content.value = the user-provided text
//   - style.font.font_size / font_family / font_weight (Poppins defaults
//     to match the existing `centered_text()` helper in
//     content/text/text_helpers_centered.hpp)
//   - style.color = white (or accent_color for kinetic_word)
//   - frame.size = layout box size
//   - frame.anchor / frame.align / frame.vertical_align = Center / Middle
//   - frame.placement = the resolved placement pin point for 1920×1080
//     (caller can override via the returned TextDefinition)
//
// The frame.placement is hard-locked to the 1920×1080 canonical canvas pin
// points to satisfy the §2B "centra un titolo in < 10 righe" criterion.
// A future multi-canvas-resolution follow-up can parameterize position
// via the `CanvasInfo` + `resolve_placement_origin` API; this is the
// §6 of the M1.8 plan forward-point, deferred to keep §3C minimal.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_definition.hpp>       // TextDefinition, TextContent
#include <chronon3d/text/text_placement.hpp>        // TextPlacement, TextPlacementKind
#include <chronon3d/scene/builders/builder_params.hpp>  // TextContent, FontSpec
#include <chronon3d/math/glm_types.hpp>             // Vec2, Vec3
#include <chronon3d/math/color.hpp>                  // Color

#include <optional>
#include <string>
#include <utility>

namespace chronon3d::presets::text {

// ═══════════════════════════════════════════════════════════════════════════
// 1. title_centered
// ═══════════════════════════════════════════════════════════════════════════

/// Canonical hero/centered title preset.  Places the text at the canvas
/// center on the 1920×1080 default canvas, Poppins-Bold 96pt (matches
/// the `centered_text()` helper default).
///
/// Parameters:
///   text       — the string to render
///   font_size  — target font size in canvas pixels (default 96.0f)
///   max_width  — optional max layout box width; std::nullopt means
///                no constraint (default box 900×160; same as
///                centered_text() default)
///
/// Pin point: (960, 540) — canvas center for 1920×1080.
///   (A future FU10 follow-up can parameterize the canvas via
///   `resolve_placement_origin` + `CanvasInfo`; §3C keeps it hard-locked
///   for simplicity.  Callers needing custom canvas size can override
///   `def.frame.placement` after the call.)
[[nodiscard]] inline TextDefinition title_centered(
    std::string text,
    float font_size = 96.0f,
    std::optional<float> max_width = std::nullopt) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 700;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    // Layout box: width = max_width if set, else 900.0f default
    //             height = 160.0f default (matches centered_text()).
    const float w = max_width.value_or(900.0f);
    def.frame.size            = Vec2{w, 160.0f};
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 0.95f;
    // Hard-locked pin point for 1920×1080 (canonical canvas).
    def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
                                        Vec2{960.0f, 540.0f}};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. subtitle_bottom
// ═══════════════════════════════════════════════════════════════════════════

/// Subtitle at the bottom safe area.  Poppins-SemiBold 48pt, white on
/// 5% safe-margin black background (sRGB-grey, no fill — visual
/// sophistication deferred to user CSS).
///
/// Pin point: (960, 1026) — bottom safe area for 1920×1080 (5% margin
/// = 54px from bottom edge; 1080 - 54 = 1026).
[[nodiscard]] inline TextDefinition subtitle_bottom(
    std::string text,
    float font_size = 48.0f) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Regular.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 600;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{0.9f, 0.9f, 0.9f, 1.0f};
    def.frame.size            = Vec2{1200.0f, 100.0f};
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.05f;
    // Pin point: 1920×1080, SafeAreaBottom (5% margin).
    def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
                                        Vec2{960.0f, 1026.0f}};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. caption_safe_area
// ═══════════════════════════════════════════════════════════════════════════

/// Centered caption inside the safe area bounds.  Poppins-Medium 36pt
/// (smaller than subtitle to leave breathing room; the 5% safe-area
/// margin applies on all 4 sides).
///
/// Pin point: (960, 540) — safe-area center for 1920×1080 (same as
/// canvas center when safe area is symmetric, but documents the intent).
[[nodiscard]] inline TextDefinition caption_safe_area(
    std::string text,
    float font_size = 36.0f) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Regular.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 500;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    def.frame.size            = Vec2{1640.0f, 80.0f};   // 1920 - 2*140
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.15f;
    // Pin point: 1920×1080, SafeAreaCenter (5% margin symmetric).
    def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
                                        Vec2{960.0f, 540.0f}};
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
/// Pin point: (960, 540) — canvas center for 1920×1080.
[[nodiscard]] inline TextDefinition kinetic_word(
    std::string text,
    float font_size = 120.0f,
    std::optional<Color> accent_color = std::nullopt) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 900;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = accent_color.value_or(Color{1.0f, 1.0f, 1.0f, 1.0f});
    def.frame.size            = Vec2{1600.0f, 240.0f};
    def.frame.anchor          = TextAnchor::Center;
    def.frame.align           = TextAlign::Center;
    def.frame.vertical_align  = VerticalAlign::Middle;
    def.frame.wrap            = TextWrap::None;  // single-word
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 0.90f;
    // Pin point: 1920×1080, CanvasCenter.
    def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
                                        Vec2{960.0f, 540.0f}};
    return def;
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. lower_third
// ═══════════════════════════════════════════════════════════════════════════

/// Lower-third broadcast-style name plate.  Poppins-Bold 42pt, left-aligned
/// to the bottom-left safe area, white on dark-grey caption (visual
/// sophistication deferred to user).
///
/// Pin point: (140, 920) — bottom-left safe area for 1920×1080 (5% margin
/// = 96px from left, 1080 - 160 = 920; matches the 1640-wide caption box
/// starting at x=140 with 80px breathing room from the left edge).
[[nodiscard]] inline TextDefinition lower_third(
    std::string text,
    float font_size = 42.0f) noexcept
{
    TextDefinition def{};
    def.content.value   = std::move(text);
    def.style.font.font_path = "assets/fonts/Poppins-Bold.ttf";
    def.style.font.font_family = "Poppins";
    def.style.font.font_weight = 700;
    def.style.font.font_style  = "normal";
    def.style.font.font_size   = font_size;
    def.style.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    def.frame.size            = Vec2{1640.0f, 100.0f};
    def.frame.anchor          = TextAnchor::TopLeft;  // text starts at pin
    def.frame.align           = TextAlign::Left;
    def.frame.vertical_align  = VerticalAlign::Top;
    def.frame.wrap            = TextWrap::Word;
    def.frame.overflow        = TextOverflow::Clip;
    def.frame.line_height     = 1.05f;
    // Pin point: 1920×1080, SafeAreaLeft (5% margin = 96px from left,
    // vertically centered in safe area at y=540 minus 160 vertical
    // offset for the lower-third positioning = 920).
    def.frame.placement = TextPlacement{TextPlacementKind::Absolute,
                                        Vec2{140.0f, 920.0f}};
    return def;
}

} // namespace chronon3d::presets::text
