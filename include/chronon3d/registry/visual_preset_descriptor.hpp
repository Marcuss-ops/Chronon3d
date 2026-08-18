// ==============================================================================
// include/chronon3d/registry/visual_preset_descriptor.hpp
//
// VISUAL-SSOT-01 — Single-registry VisualPresetDescriptor.
//
// PUBLIC API — defines `VisualPresetDescriptor { id, version,
// supported_layer, base_preset, style, anchor, animation,
// fallback_anchors, capabilities }` plus the value types it aggregates:
// `VisualLayerKind`, `VisualStyle`, `AnchorSpec`, `AnimationSpec`.
//
// This is the SINGLE canonical descriptor type for overlay-level visual
// presets (caption cards, lower thirds, organization/location cards,
// image focus treatments).  It is intentionally a DIFFERENT abstraction
// from `TextPresetDescriptor` (text-run typography motion): the visual
// preset describes how a whole overlay layer is painted, anchored and
// animated, whereas the text preset describes the per-glyph motion recipe.
//
// Ownership model (see ADR-029):
//   PipelineGen  → decides WHAT to show (semantic_role), editorial only.
//   RenderingGen → transports + executes; it must NOT know that
//                  PERSON means `lower_third_safe`.
//   Chronon      → decides HOW to render it: this registry is the single
//                  source of truth for the preset's default style, anchor,
//                  animation, fallback anchors and capabilities.
//
// Anti-duplication-guardrail: there is exactly ONE visual-preset
// descriptor type and ONE registry type.  The semantic→preset mapping
// lives in PipelineGen's SemanticOverlayResolver (editorial decision),
// NOT in a second visual registry inside RenderingGen.
// ==============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::registry {

// ── VisualLayerKind ─────────────────────────────────────────────────────────
//
// The layer primitive a preset is materialized onto.  Mirrors the
// render-plan layer-type vocabulary (image / video / text / color) so a
// resolver can reject a preset applied to the wrong layer at preflight
// time instead of silently falling back to a text primitive.
enum class VisualLayerKind : std::uint8_t { Image, Video, Text, Color };

// snake_case-lowercase ASCII ↔ enum (no Unicode).
[[nodiscard]] inline std::string_view
to_string_view(VisualLayerKind k) noexcept {
    switch (k) {
        case VisualLayerKind::Image: return "image";
        case VisualLayerKind::Video: return "video";
        case VisualLayerKind::Text:  return "text";
        case VisualLayerKind::Color: return "color";
    }
    return "unknown";
}

[[nodiscard]] inline std::string
to_string(VisualLayerKind k) {
    return std::string{to_string_view(k)};
}

// ── VisualStyle ─────────────────────────────────────────────────────────────
//
// The preset's canonical default paint recipe.  These are the defaults a
// StyleResolver merges with per-job overrides carried by the render-plan
// `style` block:
//
//   preset defaults + job overrides = ResolvedVisualStyle
//
// so a job only ships the fields it deviates from, never a full copy of the
// 25-property style.  Color fields use canonical hex (`#RRGGBB`); empty
// strings mean "renderer default".  Numeric fields are std::optional so
// absence is distinguishable from an explicit zero.
//
// Readability is LOCAL-ONLY (ADR-029): the text card's background/shadow/
// stroke, never a global contrast veil darkening the whole frame. Adaptive
// (content-aware) contrast is deferred — see TICKET-VISUAL-PRESET-ADAPTIVE-
// CONTRAST.
struct VisualStyle {
    std::string font_family;                 // "DejaVu Sans" — resolved as a font asset
    // Canonical font asset logical path (relative to the asset root). This
    // is the byte-identity anchor: the asset manifest hashes it (sha256) so
    // every worker resolves identical bytes (no system-font / DejaVuSans
    // fallback). Empty = the materialization preset's own default.
    std::string font_asset;                  // "assets/fonts/DejaVuSans.ttf"
    std::optional<int> font_weight;          // 700
    std::optional<float> font_size;          // 58
    std::string fill;                        // "#FFFFFF"
    std::string stroke_color;                // "#000000"
    std::optional<float> stroke_width;       // 2
    std::string shadow_color;                // "#000000"
    std::optional<float> shadow_opacity;     // 0.65
    std::optional<float> shadow_blur;        // 16
    std::optional<std::array<float, 2>> shadow_offset;   // {0, 6}
    std::string background_color;            // "#050509"
    std::optional<float> background_opacity; // 0.86
    std::optional<float> radius;             // 12
    std::optional<std::array<float, 2>> padding;         // {24, 14}
};

// ── AnchorSpec ──────────────────────────────────────────────────────────────
//
// Layout INTENT, not absolute coordinates.  The anchor resolver maps the
// intent to final x/y through the pipeline:
//
//   canvas → safe area → content bounds → anchor resolver → final x/y
//
// Anchor types form the closed layout-intent vocabulary enforced by the
// render-plan schema (`anchor.type` enum): center, safe_area, lower_third,
// lower_left, lower_right, top_left, top_right, bottom_left, bottom_right,
// image_left, image_right.  Numeric coordinates stay available as a separate
// per-job `offset` override; they are never the canonical placement.
struct AnchorSpec {
    std::string type;                 // "center", "safe_area", "lower_third", ...
    float safe_margin{0.06f};         // fraction of the canvas reserved per side
    std::string alignment{"left"};    // "left" | "center" | "right"
};

// ── AnimationSpec ───────────────────────────────────────────────────────────
//
// Motion intent.  `preset` selects the canonical animation; `unit` selects
// the text-run selector scope (word / glyph / line) so the animation is
// wired through Chronon's text pipeline:
//
//   TextRun → selector → Word/Glyph/Line → animator
struct AnimationSpec {
    std::string preset;                      // "fade_in", "active_word_pop", ...
    std::string unit{"word"};                // "word" | "glyph" | "line"
    std::optional<int> enter_duration_frames; // 8
    std::optional<int> exit_duration_frames;  // 6
};

// ── VisualPresetDescriptor ──────────────────────────────────────────────────
//
// Canonical descriptor for an overlay-level visual preset.  Layout matches
// the ADR-029 spec:
//
//   {
//     id:               unique snake_case key (mirrors map index);
//     version:          preset schema version;
//     supported_layer:  VisualLayerKind the preset is valid for;
//     base_preset:      canonical TEXT materializer id the preset lowers
//                       onto (empty for image/video presets);
//     style:            VisualStyle (default paint recipe);
//     anchor:           AnchorSpec (preferred layout intent);
//     animation:        AnimationSpec (motion intent);
//     fallback_anchors: ordered fallback intents when the preferred
//                       anchor collides or exits the safe area;
//     capabilities:     closed capability tags ("card",
//                       "local_background", "collision_avoid", ...).
//   }
struct VisualPresetDescriptor {
    std::string id;                                 // O(1) lookup key — sole preset identity.
    // Editorial category metadata. It does not resolve or render anything;
    // Chronon still owns the single visual preset recipe.
    std::string semantic_role;                      // image | name | important_phrase
    int version{1};                                 // preset schema version.
    VisualLayerKind supported_layer{VisualLayerKind::Text};
    // Canonical TEXT materializer id this preset lowers onto
    // ("caption_safe_area", "kinetic_word", "subtitle_bottom",
    // "lower_third"). This is the SINGLE place the preset→materializer
    // mapping lives: the render-plan compiler consumes it instead of
    // keeping a parallel table. Empty for non-text presets (image/video),
    // which materialize through their own layer branch.
    std::string base_preset;
    VisualStyle style;                              // default paint recipe.
    AnchorSpec anchor;                              // preferred layout intent.
    AnimationSpec animation;                        // motion intent.
    std::vector<std::string> fallback_anchors;      // preferred → fallback order.
    std::vector<std::string> capabilities;          // closed capability tags.
    // ── Image geometry defaults (image presets only) ──────────────────────
    // Canonical content box the image materializes with when the plan carries
    // no explicit box/fit. Absent box → full canvas; empty fit → renderer
    // default (cover). This is the ONE place image presets own their box;
    // PipelineGen no longer hardcodes IMAGE_OVERLAY/PRODUCT/LOGO geometry.
    std::optional<float> box_width;
    std::optional<float> box_height;
    std::string fit;                                // "cover" | "contain" | "stretch" | "none"
};

} // namespace chronon3d::registry
