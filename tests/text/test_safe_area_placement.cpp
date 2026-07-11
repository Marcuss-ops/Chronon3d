// test_safe_area_placement.cpp — Unit tests for the SafeArea placement family
//
// ADR-019 Decision 3 + M1.8 §3B (Placement Leggibili + Safe Areas):
// Extends `TextPlacementKind` with the symmetric SafeArea{Left, Right}
// placements (the SafeArea{Top, Bottom, Center} variants already existed
// from Phase A.2) and exposes the user-facing `SafeAreaEnum`
// resolution entry point `resolve_safe_area(SafeAreaEnum) -> TextPlacement`.
//
// Per the user spec for M1.8 §4 the test matrix is 8 sub-cases:
//   4 placements (SafeAreaTop / SafeAreaBottom / SafeAreaLeft / SafeAreaRight)
//   × 2 canvases (1920×1080 landscape + 1080×1920 portrait)
//   = 8 pin-point assertions locked.
//
// In addition we lock the `resolve_safe_area()` switch exhaustively
// (5 SafeAreaEnum values → 5 TextPlacement values) and the cross-link
// invariant SafeAreaCenter pin matches both direct (via SafeAreaKind) and
// indirect (via SafeAreaEnum::Center) resolution paths.
//
// Pure math harness — no Framebuffer / compositor / GPU dependency.
// Compiles in any preset that turns on `CHRONON3D_BUILD_TESTS`
// (matched by `tests/safe_area_placement_tests.cmake` registration).

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement.hpp>           // SafeAreaEnum, TextPlacementKind, TextPlacement, SafeAreaPreset
#include <chronon3d/text/text_placement_resolver.hpp>   // resolve_placement_origin, resolve_safe_area, CanvasInfo

#include <cmath>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

static constexpr f32 kEps = 0.01f;

static bool vec2_near(Vec2 a, Vec2 b, f32 eps = kEps) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}

// Landscape 16:9 canvas with default 5% safe-area margins.
// Per SafeAreaPreset::Landscape16x9: width=1920, height=1080,
// safe_margin_top=54 (5% of 1080), safe_margin_bottom=54, safe_margin_left=96 (5% of 1920),
// safe_margin_right=96.
static CanvasInfo landscape_canvas() {
    return CanvasInfo{1920.0f, 1080.0f, 54.0f, 54.0f, 96.0f, 96.0f};
}

// Portrait 9:16 canvas with default 5% safe-area margins.
// Per SafeAreaPreset::Portrait9x16: width=1080, height=1920,
// safe_margin_top=96 (5% of 1920), safe_margin_bottom=96, safe_margin_left=54 (5% of 1080),
// safe_margin_right=54.
static CanvasInfo portrait_canvas() {
    return CanvasInfo{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 1: SafeAreaTop on 1920×1080 landscape
// ═══════════════════════════════════════════════════════════════════════════
//
// Pin point: (canvas.width/2, safe_margin_top) = (960, 54).

TEST_CASE("SafeArea: SafeAreaTop on 1920×1080 lands at (960, 54)") {
    auto c = landscape_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaTop});
    // Landscape SafeAreaTop: x = canvas.width/2 = 960, y = safe_margin_top = 54
    CHECK(vec2_near(pin, {960.0f, 54.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 2: SafeAreaBottom on 1920×1080 landscape
// ═══════════════════════════════════════════════════════════════════════════
//
// Pin point: (canvas.width/2, canvas.height - safe_margin_bottom) = (960, 1026).

TEST_CASE("SafeArea: SafeAreaBottom on 1920×1080 lands at (960, 1026)") {
    auto c = landscape_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaBottom});
    // Landscape SafeAreaBottom: x = 960, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {960.0f, 1026.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 3: SafeAreaLeft on 1920×1080 landscape (NEW in M1.8 §3B)
// ═══════════════════════════════════════════════════════════════════════════
//
// Pin point: (safe_margin_left, center-of-safe-area-vertical).
// Safe-area vertical center = (safe_margin_top + (canvas.height - safe_margin_bottom)) / 2
//                          = (54 + (1080 - 54)) / 2 = (54 + 1026) / 2 = 540.

TEST_CASE("SafeArea: SafeAreaLeft on 1920×1080 lands at (96, 540)") {
    auto c = landscape_canvas();
    Vec2 box{600.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaLeft});
    // Landscape SafeAreaLeft: x = safe_margin_left = 96
    //                         y = (54 + (1080-54)) / 2 = 540 (safe-area vertical center)
    CHECK(vec2_near(pin, {96.0f, 540.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 4: SafeAreaRight on 1920×1080 landscape (NEW in M1.8 §3B)
// ═══════════════════════════════════════════════════════════════════════════
//
// Pin point: (canvas.width - safe_margin_right, center-of-safe-area-vertical).

TEST_CASE("SafeArea: SafeAreaRight on 1920×1080 lands at (1824, 540)") {
    auto c = landscape_canvas();
    Vec2 box{600.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaRight});
    // Landscape SafeAreaRight: x = 1920 - 96 = 1824
    //                          y = (54 + (1080-54)) / 2 = 540
    CHECK(vec2_near(pin, {1824.0f, 540.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 5: SafeAreaTop on 1080×1920 portrait
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SafeArea: SafeAreaTop on 1080×1920 lands at (540, 96)") {
    auto c = portrait_canvas();
    Vec2 box{900.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaTop});
    // Portrait SafeAreaTop: x = canvas.width/2 = 540, y = safe_margin_top = 96
    CHECK(vec2_near(pin, {540.0f, 96.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 6: SafeAreaBottom on 1080×1920 portrait
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SafeArea: SafeAreaBottom on 1080×1920 lands at (540, 1824)") {
    auto c = portrait_canvas();
    Vec2 box{900.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaBottom});
    // Portrait SafeAreaBottom: x = 540, y = 1920 - 96 = 1824
    CHECK(vec2_near(pin, {540.0f, 1824.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 7: SafeAreaLeft on 1080×1920 portrait (NEW in M1.8 §3B)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SafeArea: SafeAreaLeft on 1080×1920 lands at (54, 960)") {
    auto c = portrait_canvas();
    Vec2 box{400.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaLeft});
    // Portrait SafeAreaLeft: x = safe_margin_left = 54
    //                        y = (96 + (1920-96)) / 2 = (96 + 1824) / 2 = 960
    CHECK(vec2_near(pin, {54.0f, 960.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// Sub-case 8: SafeAreaRight on 1080×1920 portrait (NEW in M1.8 §3B)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SafeArea: SafeAreaRight on 1080×1920 lands at (1026, 960)") {
    auto c = portrait_canvas();
    Vec2 box{400.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::SafeAreaRight});
    // Portrait SafeAreaRight: x = 1080 - 54 = 1026
    //                         y = (96 + (1920-96)) / 2 = 960
    CHECK(vec2_near(pin, {1026.0f, 960.0f}));
}

// ═══════════════════════════════════════════════════════════════════════════
// resolve_safe_area — exhaustive switch lock (5 SafeAreaEnum → TextPlacement)
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.8 §3B exposes `resolve_safe_area(SafeAreaEnum)` as the user-facing
// entry point for the SafeArea family.  The mapping is exhaustive
// (5 SafeAreaEnum values → 5 TextPlacement{SafeArea*} values).
// No offset is applied (offset = {0, 0}).
//
// This locks the switch end-to-end so any future case addition or
// rename takes the form of a compile error + this test surface
// (not a silent mapping drift).

TEST_CASE("SafeArea: resolve_safe_area exhaustively maps 5 SafeAreaEnum → TextPlacement") {
    // Sweep all 5 SafeAreaEnum values.  The resolved TextPlacement's
    // kind must match the expected TextPlacementKind and the offset
    // must be exactly zero (no additive nudge on the resolved pin).
    SUBCASE("Top → SafeAreaTop, zero offset") {
        auto p = resolve_safe_area(SafeAreaEnum::Top);
        CHECK(p.kind == TextPlacementKind::SafeAreaTop);
        CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
    }
    SUBCASE("Bottom → SafeAreaBottom, zero offset") {
        auto p = resolve_safe_area(SafeAreaEnum::Bottom);
        CHECK(p.kind == TextPlacementKind::SafeAreaBottom);
        CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
    }
    SUBCASE("Left → SafeAreaLeft, zero offset (M1.8 §3B new)") {
        auto p = resolve_safe_area(SafeAreaEnum::Left);
        CHECK(p.kind == TextPlacementKind::SafeAreaLeft);
        CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
    }
    SUBCASE("Right → SafeAreaRight, zero offset (M1.8 §3B new)") {
        auto p = resolve_safe_area(SafeAreaEnum::Right);
        CHECK(p.kind == TextPlacementKind::SafeAreaRight);
        CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
    }
    SUBCASE("Center → SafeAreaCenter, zero offset") {
        auto p = resolve_safe_area(SafeAreaEnum::Center);
        CHECK(p.kind == TextPlacementKind::SafeAreaCenter);
        CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Cross-link: SafeAreaEnum resolution → resolve_placement_origin equivalence
// ═══════════════════════════════════════════════════════════════════════════
//
// For each SafeAreaEnum value, *both* the SafeAreaKind direct form
// (e.g. TextPlacement{SafeAreaTop}) and the SafeAreaEnum form via
// resolve_safe_area() must produce the same pin point on the same
// canvas.  This locks the cross-link: no parallel computation path
// for the safe-area family (AGENTS.md §anti-duplicazione).

TEST_CASE("SafeArea: SafeAreaEnum→TextPlacement→pin-equivalence on 1920×1080") {
    auto c = landscape_canvas();
    Vec2 box{800.0f, 200.0f};

    // 5 SafeAreaEnum values × their direct TextPlacement{Kind} form.
    struct CasePack { SafeAreaEnum side; TextPlacementKind direct; Vec2 expected; };
    const CasePack cases[] = {
        // Top: landscape centered-top
        {SafeAreaEnum::Top,    TextPlacementKind::SafeAreaTop,    {960.0f, 54.0f}},
        // Bottom: landscape centered-bottom
        {SafeAreaEnum::Bottom, TextPlacementKind::SafeAreaBottom, {960.0f, 1026.0f}},
        // Left: landscape left-mid (NEW M1.8 §3B)
        {SafeAreaEnum::Left,   TextPlacementKind::SafeAreaLeft,   {96.0f, 540.0f}},
        // Right: landscape right-mid (NEW M1.8 §3B)
        {SafeAreaEnum::Right,  TextPlacementKind::SafeAreaRight,  {1824.0f, 540.0f}},
        // Center: landscape safe-area center (unchanged)
        {SafeAreaEnum::Center, TextPlacementKind::SafeAreaCenter, {960.0f, 540.0f}},
    };
    for (const auto& tc : cases) {
        // Path A: SafeAreaEnum → resolve_safe_area → pin.
        auto p_via_enum = resolve_safe_area(tc.side);
        auto pin_via_enum = resolve_placement_origin(c, box, p_via_enum);

        // Path B: direct SafeArea{Kind} → pin (no SafeAreaEnum intermediate).
        auto pin_direct   = resolve_placement_origin(c, box,
                                     TextPlacement{tc.direct});

        // Both paths must converge on the same pin point.
        CHECK(vec2_near(pin_via_enum, tc.expected));
        CHECK(vec2_near(pin_via_enum, pin_direct));
    }
}

TEST_CASE("SafeArea: SafeAreaEnum→TextPlacement→pin-equivalence on 1080×1920") {
    auto c = portrait_canvas();
    Vec2 box{400.0f, 200.0f};

    struct CasePack { SafeAreaEnum side; TextPlacementKind direct; Vec2 expected; };
    const CasePack cases[] = {
        {SafeAreaEnum::Top,    TextPlacementKind::SafeAreaTop,    {540.0f, 96.0f}},
        {SafeAreaEnum::Bottom, TextPlacementKind::SafeAreaBottom, {540.0f, 1824.0f}},
        {SafeAreaEnum::Left,   TextPlacementKind::SafeAreaLeft,   {54.0f, 960.0f}},
        {SafeAreaEnum::Right,  TextPlacementKind::SafeAreaRight,  {1026.0f, 960.0f}},
        {SafeAreaEnum::Center, TextPlacementKind::SafeAreaCenter, {540.0f, 960.0f}},
    };
    for (const auto& tc : cases) {
        auto p_via_enum = resolve_safe_area(tc.side);
        auto pin_via_enum = resolve_placement_origin(c, box, p_via_enum);

        auto pin_direct   = resolve_placement_origin(c, box,
                                     TextPlacement{tc.direct});

        CHECK(vec2_near(pin_via_enum, tc.expected));
        CHECK(vec2_near(pin_via_enum, pin_direct));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Determinism — 100 consecutive resolutions are bit-identical
// ═══════════════════════════════════════════════════════════════════════════
//
// The user-facing SafeAreaEnum resolution path must be deterministic:
// no allocation, no global state, no time-dependent math.  Re-running
// resolve_safe_area(...) 100 times must produce the same
// TextPlacement struct each time (locked by operator==).

TEST_CASE("SafeArea: resolve_safe_area is deterministic across 100 calls") {
    const auto first = resolve_safe_area(SafeAreaEnum::Left);
    for (int i = 0; i < 100; ++i) {
        const auto p = resolve_safe_area(SafeAreaEnum::Left);
        CHECK(p == first);  // operator== (text_placement.hpp) byte-equivalent
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Additive-offset: SafeAreaEnum resolution respects additive offset
// ═══════════════════════════════════════════════════════════════════════════
//
// resolve_safe_area() returns a TextPlacement with zero offset.  When
// the caller adds a non-zero offset to the returned struct, the
// resolver MUST apply it as an additive displacement (per the
// `pin += placement.offset` rule in resolve_placement_origin).
//
// This locks the additive-offset interaction with the user-facing
// safe-area entry point — common authoring pattern is
// `auto p = resolve_safe_area(SafeAreaEnum::Bottom); p.offset = {0, -12};`
// to nudge a subtitle up by 12 pixels.

TEST_CASE("SafeArea: SafeAreaBottom + offset {0, -12} → pin (960, 1014) on 1920×1080") {
    auto c = landscape_canvas();
    Vec2 box{800.0f, 100.0f};
    auto p = resolve_safe_area(SafeAreaEnum::Bottom);
    p.offset = {0.0f, -12.0f};  // nudge up 12 pixels (subtitle lift)

    auto pin = resolve_placement_origin(c, box, p);
    // SafeAreaBottom pin (960, 1026) + offset (0, -12) = (960, 1014)
    CHECK(vec2_near(pin, {960.0f, 1014.0f}));
}

TEST_CASE("SafeArea: SafeAreaLeft + offset {20, 0} → pin (116, 540) on 1920×1080") {
    auto c = landscape_canvas();
    Vec2 box{800.0f, 100.0f};
    auto p = resolve_safe_area(SafeAreaEnum::Left);
    p.offset = {20.0f, 0.0f};  // nudge right 20 pixels (breathing room from edge)

    auto pin = resolve_placement_origin(c, box, p);
    // SafeAreaLeft pin (96, 540) + offset (20, 0) = (116, 540)
    CHECK(vec2_near(pin, {116.0f, 540.0f}));
}
