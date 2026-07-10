#pragma once

// ============================================================================
// text_placement.hpp — Canonical text placement authoring type
//
// Phase A.2 of the text V1 cleanup plan.  This is the SINGLE source of truth
// for how a text frame is positioned within the Canvas coordinate space.
//
// TextPlacement bundles two pieces of information that were previously split
// across the resolver signature (a `TextPlacement` enum + a separate `Vec2
// offset` parameter) into one cohesive rendering-specification item.  This
// eliminates the `TextSpec.position` ambiguity — instead of two loose fields
// (position enum + position offset) the authoring layer now has ONE struct.
//
// Lifecycle:
//   Phase A.1 (text_definition.hpp)   — TextDefinition struct (authoring DTO)
//   Phase A.2 (text_placement.hpp)    — THIS FILE: TextPlacement authoring type
//   Phase A.3 (effects/animation)     — TextEffects, TextAnimation placeholders
//   Phase B (migration)               — All call sites migrate to TextPlacement
//   Phase C (builder)                 — Simple builder API: .place(TextPlacement{…})
//   Phase D (gate)                    — Anti-legacy gate blocks old enum+offset
//
// Anti-duplicazione honour:
//   - The kind enum is named `TextPlacementKind` to free `TextPlacement` for
//     the bundling struct (it cannot be both an enum and a struct in C++).
//   - The existing `text_placement_resolver.hpp` `enum class TextPlacement`
//     is removed and replaced by `TextPlacementKind` (14 variants, no
//     behavior change in the resolver — only the API surface).
//   - The 12 original variants are kept + `Center` + `SafeAreaCenter` added
//     for a total of 14.  The `Absolute` variant has no payload; the
//     absolute position is the `offset` field of the struct (matching the
//     existing resolver semantics where `Absolute` means "pin = offset"
//     rather than "pin += offset").
//
// Phase A.2 migration scope (this commit):
//   - Introduces the new `TextPlacement` struct (kind + offset bundled).
//   - Migrates the resolver API (the canonical implementation).
//   - Migrates `Text::place()` in `include/chronon3d/authoring/text.hpp`
//     (the single include/ consumer of the resolver).
//   - Migrates `tests/text/test_text_placement_resolver.cpp` (the only
//     in-tree test that exercises the resolver directly).
//
// External consumer paths (content/, examples/, tests/install_consumer/)
// do NOT currently call `Text::place()` (verified via repo-wide grep), so
// this commit has no external blast radius.  Phase B will migrate any
// newly-added callers as they appear.
// ============================================================================

#include <chronon3d/math/glm_types.hpp>  // Vec2

namespace chronon3d {

// ── TextPlacementKind — high-level placement semantics ────────────────────
//
// Determines where the text box is positioned relative to the canvas or
// layer.  Each variant produces a different Vec2 origin in Canvas coords.
//
// Usage:
//   TextPlacement{TextPlacementKind::CanvasCenter}    // center of canvas
//   TextPlacement{TextPlacementKind::SafeAreaBottom}  // bottom safe area
//   TextPlacement{TextPlacementKind::Absolute, {960, 540}}  // explicit position
//
// Semantics for each variant (pin point = where the box's anchor sits):
//   CanvasCenter      — (canvas.width/2, canvas.height/2)
//   TopLeft           — (safe_margin_left,  safe_margin_top)
//   TopCenter         — (canvas.width/2,    safe_margin_top)
//   TopRight          — (canvas.width - safe_margin_right,  safe_margin_top)
//   CenterLeft        — (safe_margin_left,  canvas.height/2)
//   Center            — (canvas.width/2,    canvas.height/2)  [Phase A.2 addition]
//   CenterRight       — (canvas.width - safe_margin_right,  canvas.height/2)
//   BottomLeft        — (safe_margin_left,  canvas.height - safe_margin_bottom)
//   BottomCenter      — (canvas.width/2,    canvas.height - safe_margin_bottom)
//   BottomRight       — (canvas.width - safe_margin_right,  canvas.height - safe_margin_bottom)
//   SafeAreaTop       — (canvas.width/2,    safe_margin_top)
//   SafeAreaBottom    — (canvas.width/2,    canvas.height - safe_margin_bottom)
//   SafeAreaCenter    — center of safe area bounds  [Phase A.2 addition]
//   Absolute          — offset (the explicit position, additive offset ignored)
//
// Note on `Center` vs `CanvasCenter`:
//   The two variants resolve to the same pin point today (both = canvas
//   center).  `Center` is included in the 14-variant Phase A.2 spec for
//   authoring clarity ("I want the center of the canvas, no safe-area
//   consideration") vs `CanvasCenter` (semantic alias).  Future
//   placements may distinguish them (e.g., `Center` = canvas center
//   ignoring safe area, `CanvasCenter` = center of the *visible* canvas
//   after safe-area deduction).  Until that distinction lands, the two
//   behave identically — the duplication is intentional and tracked.
//
enum class TextPlacementKind : u8 {
    CanvasCenter,
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,                // Phase A.2 addition: canvas center (alias of CanvasCenter today)
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    SafeAreaTop,
    SafeAreaBottom,
    SafeAreaCenter,        // Phase A.2 addition: center of safe area bounds
    Absolute,              // The `offset` field IS the pin point
};

// ── TextPlacement — placement + offset authoring struct ───────────────────
//
// Bundles the placement kind and the user-specified offset into a single
// authoring type.  Replaces the previous `enum TextPlacement` + `Vec2 offset`
// resolver parameters with one cohesive value.
//
// Default value (CanvasCenter, zero offset) is the most common authoring
// intent: place the text in the middle of the canvas, no extra nudging.
//
// Fields:
//   kind    — Which anchor point on the canvas the box should pin to.
//   offset  — Additive offset from the resolved pin point (pixels).
//             For `Absolute` placement, the offset IS the pin point and
//             no other placement math is applied.
//
// Constructors:
//   TextPlacement{}                              → CanvasCenter, {0,0}
//   TextPlacement{TextPlacementKind::TopLeft}    → TopLeft,    {0,0}
//   TextPlacement{TextPlacementKind::Absolute, {x, y}}
//                                                 → Absolute,   {x, y}
//
// `operator==` is provided so the struct can be used as a hash/equality
// key in cache invalidation (FrameCache, layout cache, etc.) without
// pulling in a per-field comparison helper.  Trivially copyable, the
// comparison is byte-equivalent for the same field values.
//
struct TextPlacement {
    TextPlacementKind kind    = TextPlacementKind::CanvasCenter;
    Vec2              offset  = {0.0f, 0.0f};
};

[[nodiscard]] inline bool operator==(const TextPlacement& a, const TextPlacement& b) noexcept {
    return a.kind == b.kind
        && a.offset.x == b.offset.x
        && a.offset.y == b.offset.y;
}

[[nodiscard]] inline bool operator!=(const TextPlacement& a, const TextPlacement& b) noexcept {
    return !(a == b);
}

// ═══════════════════════════════════════════════════════════════════════════
// SafeAreaPreset — F3.B aspect-ratio-aware safe area configuration
// ═══════════════════════════════════════════════════════════════════════════
//
// Provides predefined safe area margins for common aspect ratios.
// Safe areas are expressed as fractions of the canvas dimension (0..1).
// The 5% default matches industry-standard title/action-safe zones.
//
// Presets:
//   Landscape16x9  — 1920×1080, 5% margins (96px horizontal, 54px vertical)
//   Portrait9x16   — 1080×1920, 5% margins (54px horizontal, 96px vertical)
//   Square1x1      — 1080×1080, 5% margins (54px each side)
//   Landscape4x3   — 1440×1080, 5% margins (72px horizontal, 54px vertical)
//
// Usage:
//   CanvasInfo canvas = SafeAreaPreset::Landscape16x9.canvas(1920, 1080);
//   CanvasInfo canvas = SafeAreaPreset::Portrait9x16.canvas(1080, 1920);
//
struct SafeAreaPreset {
    f32 margin_top_fraction{0.05f};
    f32 margin_bottom_fraction{0.05f};
    f32 margin_left_fraction{0.05f};
    f32 margin_right_fraction{0.05f};

    /// Predefined presets with standard 5% safe-area margins.
    /// All presets use identical 5% fractions — the differentiation
    /// comes from canvas dimensions passed to CanvasInfo::with_safe_area().
    /// The preset names document the aspect-ratio INTENT (e.g.,
    /// Portrait9x16 ensures margins are proportional to a 1080×1920
    /// canvas).  Adding more presets with different fractions is
    /// appropriate when aspect-ratio-specific safe areas are needed.
    static const SafeAreaPreset Landscape16x9;
    static const SafeAreaPreset Portrait9x16;
    static const SafeAreaPreset Square1x1;
    static const SafeAreaPreset Landscape4x3;
};

} // namespace chronon3d
