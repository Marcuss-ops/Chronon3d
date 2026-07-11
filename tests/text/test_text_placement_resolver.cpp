// test_text_placement_resolver.cpp — Tests for the canonical text placement resolver
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level.
//
// Phase A.2 of the text V1 plan: the resolver takes a single `TextPlacement`
// struct (kind + offset bundled) instead of the old `TextPlacement` enum +
// separate `Vec2 offset` parameter pair.  This file migrates all 30+ test
// cases to the new struct API.
//
// Phase A.6 (2026-07-10): the co-resident `class TextPlacementResolver`
// wrapper surface (previously tested in the now-deleted
// `ResolvedTextPlacement: TextPlacementResolver class wrapper (Phase A.3)`
// TEST_CASE) was RETIRED; the free-function surface is canonical.  This
// file now exercises exclusively `resolve_placement_origin()` and
// `resolve_text_placement()` — the canonical surface.
//
// Semantics:
//   resolve_placement_origin() returns the PIN POINT — where the box's
//   anchor point should sit on the canvas.  It is NOT the box top-left.
//   resolve_text_placement() computes box_top_left = pin_point - anchor_offset.

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement.hpp>           // TextPlacement, TextPlacementKind
#include <chronon3d/text/resolve_text_placement.hpp>    // Canonical resolver surface

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

static constexpr f32 kEps = 0.01f;

static bool vec2_near(Vec2 a, Vec2 b, f32 eps = kEps) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}

static bool mat4_near(const Mat4& a, const Mat4& b, f32 eps = kEps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a[i][j] - b[i][j]) > eps) return false;
    return true;
}

static CanvasInfo default_canvas() {
    return CanvasInfo{1920.0f, 1080.0f, 54.0f, 54.0f, 96.0f, 96.0f};
}

// ═════════════════════════════════════════════════════════════════════════
// resolve_placement_origin — returns the PIN POINT (not box top-left)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("PlacementOrigin: CanvasCenter pin = canvas center") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    // pin point = canvas center = (960, 540)
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: TopLeft pin = safe margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::TopLeft});
    CHECK(vec2_near(pin, {96.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopCenter pin = top-center") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::TopCenter});
    // x = canvas center = 960, y = safe_margin_top = 54
    CHECK(vec2_near(pin, {960.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopRight pin = top-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::TopRight});
    // x = 1920 - 96 = 1824, y = 54
    CHECK(vec2_near(pin, {1824.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: CenterLeft pin = center-left") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::CenterLeft});
    // x = 96, y = 540
    CHECK(vec2_near(pin, {96.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: Center pin = canvas center (Phase A.2 addition)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::Center});
    // x = 960, y = 540 (same as CanvasCenter but explicit)
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: CenterRight pin = center-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::CenterRight});
    // x = 1920 - 96 = 1824, y = 540
    CHECK(vec2_near(pin, {1824.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: BottomLeft pin = bottom-left minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::BottomLeft});
    // x = 96, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {96.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: BottomCenter pin = bottom-center minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::BottomCenter});
    // x = 960, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {960.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: BottomRight pin = bottom-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::BottomRight});
    // x = 1920 - 96 = 1824, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {1824.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaTop pin = top-center") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::SafeAreaTop});
    // x = 960, y = 54
    CHECK(vec2_near(pin, {960.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaBottom pin = bottom-center") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::SafeAreaBottom});
    // x = 960, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {960.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaCenter pin = center of safe area bounds (Phase A.2 addition)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::SafeAreaCenter});
    // safe area x: (96 + (1920-96))/2 = 960
    // safe area y: (54 + (1080-54))/2 = 540
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: Absolute pin = offset (no additive)") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::Absolute, {500.0f, 300.0f}});
    // For Absolute, the offset IS the pin point (no additive math)
    CHECK(vec2_near(pin, {500.0f, 300.0f}));
}

TEST_CASE("PlacementOrigin: offset is additive for non-Absolute") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    // TopLeft pin(96, 54) + offset(10, 20) = (106, 74)
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::TopLeft, {10.0f, 20.0f}});
    CHECK(vec2_near(pin, {106.0f, 74.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// resolve_text_placement — default anchor (Center)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: CanvasCenter + Center anchor — classic centered title") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter});

    // pin = (960, 540), anchor = (850, 180), origin = (110, 360)
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(360.0f));
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));
    CHECK(r.local_frame.w == doctest::Approx(360.0f));
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));
    CHECK(mat4_near(r.layer_matrix, Mat4(1.0f)));

    Mat4 expected = glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
}

TEST_CASE("TextPlacement: SafeAreaBottom + Center anchor — subtitle") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 80.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::SafeAreaBottom});

    // pin = (960, 1026), anchor = (600, 40), origin = (360, 986)
    CHECK(r.local_frame.x == doctest::Approx(360.0f));
    CHECK(r.local_frame.y == doctest::Approx(986.0f));
    CHECK(vec2_near(r.layout_origin, {360.0f, 986.0f}));
}

TEST_CASE("TextPlacement: Absolute + Center anchor") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::Absolute, {500.0f, 300.0f}});

    // pin = (500, 300), anchor = (200, 50), origin = (300, 250)
    CHECK(r.local_frame.x == doctest::Approx(300.0f));
    CHECK(r.local_frame.y == doctest::Approx(250.0f));
    CHECK(vec2_near(r.layout_origin, {300.0f, 250.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// resolve_text_placement — non-default anchors
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: CanvasCenter + TopLeft anchor — box starts at center") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter},
                                     TextAnchor::TopLeft);

    // pin = (960, 540), anchor = (0, 0), origin = (960, 540)
    // Box top-left is at canvas center — extends right and down
    CHECK(r.local_frame.x == doctest::Approx(960.0f));
    CHECK(r.local_frame.y == doctest::Approx(540.0f));
    CHECK(vec2_near(r.layout_origin, {960.0f, 540.0f}));
}

TEST_CASE("TextPlacement: CanvasCenter + BottomCenter anchor — box bottom at center") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter},
                                     TextAnchor::BottomCenter);

    // pin = (960, 540), anchor = (850, 360), origin = (110, 180)
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(180.0f));
    CHECK(vec2_near(r.layout_origin, {110.0f, 180.0f}));
}

TEST_CASE("TextPlacement: TopLeft + TopLeft anchor — box at safe margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::TopLeft},
                                     TextAnchor::TopLeft);

    // pin = (96, 54), anchor = (0, 0), origin = (96, 54)
    // Box top-left IS at the safe margin — correct!
    CHECK(r.local_frame.x == doctest::Approx(96.0f));
    CHECK(r.local_frame.y == doctest::Approx(54.0f));
    CHECK(vec2_near(r.layout_origin, {96.0f, 54.0f}));
}

TEST_CASE("TextPlacement: TopLeft + Center anchor — box center at safe margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::TopLeft},
                                     TextAnchor::Center);

    // pin = (96, 54), anchor = (400, 100), origin = (-304, -46)
    // Box center at safe margin — top-left is off-screen (expected)
    CHECK(r.local_frame.x == doctest::Approx(-304.0f));
    CHECK(r.local_frame.y == doctest::Approx(-46.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// resolve_text_placement — with parent layer_matrix
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: with layer_matrix (parent transform)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    Mat4 parent = glm::translate(Mat4(1.0f), Vec3(100.0f, 50.0f, 0.0f));
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::TopLeft},
                                     TextAnchor::TopLeft, parent);

    // pin = (96, 54), anchor = (0, 0), origin = (96, 54)
    // world_matrix = parent × T(96, 54, 0)
    Mat4 expected = parent * glm::translate(Mat4(1.0f), Vec3(96.0f, 54.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
    CHECK(mat4_near(r.layer_matrix, parent));
}

// ═════════════════════════════════════════════════════════════════════════
// 9:16 portrait canvas
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: 9:16 portrait — CanvasCenter") {
    CanvasInfo portrait{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
    Vec2 box{900.0f, 200.0f};
    auto r = resolve_text_placement(portrait, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // pin = (540, 960), anchor = (450, 100), origin = (90, 860)
    CHECK(r.local_frame.x == doctest::Approx(90.0f));
    CHECK(r.local_frame.y == doctest::Approx(860.0f));
    CHECK(vec2_near(r.layout_origin, {90.0f, 860.0f}));
}

TEST_CASE("TextPlacement: 9:16 portrait — SafeAreaBottom") {
    CanvasInfo portrait{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
    Vec2 box{900.0f, 120.0f};
    auto r = resolve_text_placement(portrait, box,
        TextPlacement{TextPlacementKind::SafeAreaBottom});

    // pin = (540, 1920-96=1824), anchor = (450, 60), origin = (90, 1764)
    CHECK(r.local_frame.x == doctest::Approx(90.0f));
    CHECK(r.local_frame.y == doctest::Approx(1764.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// Edge cases
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: zero-size box") {
    auto c = default_canvas();
    Vec2 box{0.0f, 0.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // pin = (960, 540), anchor = (0, 0), origin = (960, 540)
    CHECK(r.local_frame.x == doctest::Approx(960.0f));
    CHECK(r.local_frame.y == doctest::Approx(540.0f));
}

TEST_CASE("TextPlacement: world_matrix transforms box-local origin correctly") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // Box-local (0,0,0,1) should map to canvas (110, 360, 0, 1)
    Vec4 local_origin = r.world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(local_origin.x == doctest::Approx(110.0f));
    CHECK(local_origin.y == doctest::Approx(360.0f));
    CHECK(local_origin.z == doctest::Approx(0.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// All placements produce valid matrices
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: all 14 variants produce valid world_matrix") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};

    // 14 variants total (Phase A.2 spec):
    //   CanvasCenter, TopLeft, TopCenter, TopRight,
    //   CenterLeft, Center, CenterRight,            // Center is the A.2 addition
    //   BottomLeft, BottomCenter, BottomRight,
    //   SafeAreaTop, SafeAreaBottom, SafeAreaCenter,  // SafeAreaCenter is the A.2 addition
    //   Absolute
    TextPlacement variants[] = {
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextPlacement{TextPlacementKind::TopLeft},
        TextPlacement{TextPlacementKind::TopCenter},
        TextPlacement{TextPlacementKind::TopRight},
        TextPlacement{TextPlacementKind::CenterLeft},
        TextPlacement{TextPlacementKind::Center},
        TextPlacement{TextPlacementKind::CenterRight},
        TextPlacement{TextPlacementKind::BottomLeft},
        TextPlacement{TextPlacementKind::BottomCenter},
        TextPlacement{TextPlacementKind::BottomRight},
        TextPlacement{TextPlacementKind::SafeAreaTop},
        TextPlacement{TextPlacementKind::SafeAreaBottom},
        TextPlacement{TextPlacementKind::SafeAreaCenter},
        TextPlacement{TextPlacementKind::Absolute, {500.0f, 300.0f}},  // Absolute needs offset
    };

    for (auto& p : variants) {
        auto r = resolve_text_placement(c, box, p);

        f32 det = glm::determinant(r.world_matrix);
        CHECK(std::abs(det) > 0.001f);
        CHECK(std::isfinite(r.layout_origin.x));
        CHECK(std::isfinite(r.layout_origin.y));
        CHECK(r.local_frame.z == doctest::Approx(box.x));
        CHECK(r.local_frame.w == doctest::Approx(box.y));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Determinism
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: deterministic across 100 calls") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 300.0f};
    auto first = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter, {10.0f, -20.0f}},
        TextAnchor::Center);
    for (int i = 0; i < 100; ++i) {
        auto r = resolve_text_placement(c, box,
            TextPlacement{TextPlacementKind::CanvasCenter, {10.0f, -20.0f}},
            TextAnchor::Center);
        CHECK(mat4_near(r.world_matrix, first.world_matrix));
        CHECK(vec2_near(r.layout_origin, first.layout_origin));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Phase A.2 — new struct API contract
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: struct aggregate init defaults (CanvasCenter, zero offset)") {
    // The default-constructed TextPlacement must be CanvasCenter + {0, 0}
    // (most common authoring intent: place at canvas center, no offset).
    TextPlacement p{};
    CHECK(p.kind == TextPlacementKind::CanvasCenter);
    CHECK(vec2_near(p.offset, {0.0f, 0.0f}));

    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, p);
    // pin = canvas center (default behavior)
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("TextPlacement: struct kind-only init (offset defaults to zero)") {
    TextPlacement p{TextPlacementKind::TopLeft};
    CHECK(p.kind == TextPlacementKind::TopLeft);
    CHECK(vec2_near(p.offset, {0.0f, 0.0f}));
}

TEST_CASE("TextPlacement: struct kind+offset init") {
    TextPlacement p{TextPlacementKind::Absolute, {123.0f, 456.0f}};
    CHECK(p.kind == TextPlacementKind::Absolute);
    CHECK(vec2_near(p.offset, {123.0f, 456.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// Phase A.3 — ResolvedTextPlacement output contract
// ═════════════════════════════════════════════════════════════════════════

// Phase A.3 spec maps these terms onto ResolvedTextPlacement fields:
//   Vec2  canvas_position     ↔ layout_origin  (Vec2; box top-left in Canvas)
//   TextFrame resolved_frame  ↔ local_frame    (Vec4 = x, y, width, height)
//   Anchor resolved_anchor   ↔ resolved_anchor (TextAnchor; input echo)
//
// These tests lock the Phase A.3 contract: the resolver must populate
// every field of ResolvedTextPlacement, including the new `resolved_anchor`
// echo of the input TextAnchor (previously discarded).

TEST_CASE("ResolvedTextPlacement: resolved_anchor echoes input TextAnchor (Phase A.3)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};

    // The resolver previously used the anchor to compute the anchor
    // offset, then discarded it.  Phase A.3 stores the input anchor
    // in `resolved_anchor` so downstream consumers can re-derive layout
    // without re-running the full resolver.
    //
    // Parametric coverage: all existing TextAnchor values must be echoed.
    // (We use a sequence of CHECKs rather than DOCTEST_VALUE_PARAMETERIZED
    // to keep the test self-contained and easy to read.)
    struct AnchorCase { TextAnchor input; };
    const AnchorCase cases[] = {
        {TextAnchor::TopLeft},
        {TextAnchor::TopCenter},
        {TextAnchor::Center},
        {TextAnchor::BottomCenter},
        {TextAnchor::BaselineLeft},
        {TextAnchor::BaselineCenter},
    };
    for (const auto& tc : cases) {
        auto r = resolve_text_placement(c, box,
            TextPlacement{TextPlacementKind::CanvasCenter}, tc.input);
        CHECK(r.resolved_anchor == tc.input);
    }
}

TEST_CASE("ResolvedTextPlacement: default anchor when not specified (Phase A.3)") {
    // The free function defaults `anchor` to TextAnchor::Center; the
    // resolved_anchor field must mirror that default.
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.resolved_anchor == TextAnchor::Center);
}

TEST_CASE("ResolvedTextPlacement: canvas_position = layout_origin (Phase A.3 spec alias)") {
    // The Phase A.3 spec term `canvas_position` maps to the existing
    // `layout_origin` field (Vec2; box top-left in Canvas coords).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // The "canvas_position" per the spec is where the text frame is
    // placed in Canvas coords.  After anchor adjustment, the box's
    // top-left lands at (110, 360) on a 1920×1080 canvas with a
    // 1700×360 box centered on the canvas.
    const Vec2 canvas_position = r.layout_origin;
    CHECK(vec2_near(canvas_position, {110.0f, 360.0f}));
}

TEST_CASE("ResolvedTextPlacement: resolved_frame = local_frame (Phase A.3 spec alias)") {
    // The Phase A.3 spec term `resolved_frame` maps to the existing
    // `local_frame` field (Vec4 = x, y, width, height).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // The "resolved_frame" per the spec is the box position + size in
    // Canvas coords.  local_frame packs (x, y, width, height).
    const Vec4 resolved_frame = r.local_frame;
    CHECK(resolved_frame.x == doctest::Approx(110.0f));
    CHECK(resolved_frame.y == doctest::Approx(360.0f));
    CHECK(resolved_frame.z == doctest::Approx(1700.0f));
    CHECK(resolved_frame.w == doctest::Approx(360.0f));
}

TEST_CASE("ResolvedTextPlacement: layer_matrix + world_matrix preserved (beyond spec)") {
    // The 2 additional fields beyond the Phase A.3 spec terms
    // (layer_matrix, world_matrix) are kept for the rendering
    // pipeline's transform composition.  Lock their population so
    // downstream consumers can rely on them.
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    Mat4 parent = glm::translate(Mat4(1.0f), Vec3(100.0f, 50.0f, 0.0f));
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::TopLeft},
        TextAnchor::TopLeft, parent);

    // layer_matrix echoes the input parent transform
    CHECK(mat4_near(r.layer_matrix, parent));
    // world_matrix = parent × T(origin) where origin = (96, 54)
    Mat4 expected = parent * glm::translate(Mat4(1.0f), Vec3(96.0f, 54.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
}

// ═════════════════════════════════════════════════════════════════════════
// ADR-019 §6 — Numerical Examples (1920×1080 canonical canvas) — explicit locks
// ═════════════════════════════════════════════════════════════════════════
//
// These 6 TEST_CASEs lock the numerical lattice published in
// [ADR-019 §6 Numerical Examples](../adr/ADR-019-text-coordinate-model.md#6-numerical-examples-19201080-canonical-canvas).
// Note the GitHub-compatible anchor `#6-...` (the `§` character is stripped by
// GitHub's markdown anchor algorithm; the link must point to `6-numerical-...`,
// not `§6-numerical-...`). Values are duplicated here verbatim so the suite is
// self-contained for CI runs that don't read the ADR markdown. When the ADR is
// updated, this block MUST be updated in lock-step.
//
// Anchor normalization: `<NUMBER> Numerical Examples` → GitHub anchor `<NUMBER>-numerical-...`.
// ADR-019 §6.1 / 6.2 / 6.3 / 6.4 / 6.5 / 6.6 → GitHub anchors `6.1-...` / `6.2-...` etc.
// (the `.` between digits is also stripped by GitHub; we link the parent §6
// anchor only once at the anchor reference above).
//
// Anti-duplication rationale (per AGENTS.md §anti-duplication rules, see also
// ROADMAP.md §V0.2 Fase-1 PIVOT precedent): some of these 6 TESTS partially
// overlap with pre-existing tests above (notably "CanvasCenter + Center
// anchor — classic centered title", "CanvasCenter + TopLeft anchor — box
// starts at center", "world_matrix transforms box-local origin correctly").
// Duplication is INTENTIONAL: the ADR-019 §6.x test names + comments form
// an explicit audit-trail link from the ADR markdown back to test code.
// Operators can `chronon3d_text_tests --test-case='*ADR-019*'` to verify all
// §6 lattice locks in one pass. The genuinely NEW coverage here is §6.4
// (rotation composition — no existing test exercises `resolve_text_placement`
// with a NON-IDENTITY `layer_matrix` rotation, which is the sole §6.4 invariant
// that exercises the `world_matrix = layer_matrix * T(origin)` composition
// formula under a real parent transform).

TEST_CASE("ADR-019 §6.1 Placement — CanvasCenter pin (960, 540) on 1920×1080") {
    // §6.1 row 1: CanvasCenter pin = (canvas.width/2, canvas.height/2)
    // Setup per ADR-019 §6: canvas 1920×1080, default safe margins
    // (96/96/54/54), box 1700×360, no offset, anchor TextAnchor::Center.
    auto c = default_canvas();                                      // 1920×1080, margins 96/96/54/54
    Vec2 box{1700.0f, 360.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("ADR-019 §6.2 Offset — TopLeft + offset {10, 20} → pin (106, 74)") {
    // §6.2 row 2: TopLeft pin + {10, 20} is ADDITIVE: (96+10, 54+20) = (106, 74)
    // Setup per ADR-019 §6.2 table: box 800×200 chosen for legibility
    // (the table does not lock box size for §6.2; pin computation is
    // independent of box size for most placements per resolver comment).
    auto c = default_canvas();                                      // 1920×1080, margins 96/96/54/54
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::TopLeft, {10.0f, 20.0f}});
    CHECK(vec2_near(pin, {106.0f, 74.0f}));
}

TEST_CASE("ADR-019 §6.3 Alignment — anchor Center default → box_top_left (110, 360)") {
    // §6.3 layout-engine contract: alignment (TextAlign + VerticalAlign)
    // is a layout-engine concern running INSIDE the box.  The resolver
    // returns the box top-left (= pin − anchor_offset).  For CanvasCenter
    // pin (960, 540) and TextAnchor::Center (anchor_offset = box/2 =
    // (850, 180)), box_top_left = (960−850, 540−180) = (110, 360).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.local_frame.x == doctest::Approx(110.0f));   // ADR-019 §6.3 box_top_left.x
    CHECK(r.local_frame.y == doctest::Approx(360.0f));   // ADR-019 §6.3 box_top_left.y
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // ADR-019 §6.3 box width
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // ADR-019 §6.3 box height
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));
}

TEST_CASE("ADR-019 §6.4 Rotation — world_matrix = rot_z(15°) × T(origin)") {
    // §6.4 composition contract: world_matrix = layer_matrix × T(origin),
    // where origin is the box top-left in Canvas coords independent of
    // rotation.  For CanvasCenter pin + Center anchor + 1700×360 box,
    // origin = (110, 360); with layer_matrix = rot_z(15°), the world_matrix
    // is the composition of rotation × translation, and `local_frame` /
    // `layout_origin` are NOT affected by rotation (they remain in Canvas
    // coords AFTER placement, before rotation).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    Mat4 rot_layer = glm::rotate(Mat4(1.0f), glm::radians(15.0f), Vec3(0, 0, 1));
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextAnchor::Center, rot_layer);

    // local_frame / layout_origin are placement-only (Canvas coords):
    // rotation is applied AFTER placement resolution, so box position +
    // box extent are INVARIANT under rotation of layer_matrix.
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(360.0f));
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));   // box width is rotation-invariant
    CHECK(r.local_frame.w == doctest::Approx(360.0f));    // box height is rotation-invariant

    // layer_matrix echoes the input rotation
    CHECK(mat4_near(r.layer_matrix, rot_layer));

    // world_matrix = rot_z(15°) × T(110, 360, 0) — matrix multiply is exact
    Mat4 expected_world = rot_layer * glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected_world));

    // Sanity: rotation pivot at canvas-center origin (960, 540) maps to itself
    // (matrix[3][0..2] for a rotation+translation anchored at origin).
    Vec4 origin_in_world = r.world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(std::isfinite(origin_in_world.x));
    CHECK(std::isfinite(origin_in_world.y));
}

TEST_CASE("ADR-019 §6.5 Anchor — TopLeft at CanvasCenter pin → box_top_left (960, 540)") {
    // §6.5 row 1: anchor = TopLeft → anchor_offset = (0, 0) → box_top_left = pin.
    // For CanvasCenter pin (960, 540), anchor TopLeft means the box's top-left
    // corner sits at the canvas center (extending right and down).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextAnchor::TopLeft);
    CHECK(r.local_frame.x == doctest::Approx(960.0f));   // ADR-019 §6.5 box_top_left.x for TopLeft
    CHECK(r.local_frame.y == doctest::Approx(540.0f));   // ADR-019 §6.5 box_top_left.y for TopLeft
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // box width unchanged
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // box height unchanged
    CHECK(vec2_near(r.layout_origin, {960.0f, 540.0f}));
    CHECK(r.resolved_anchor == TextAnchor::TopLeft);     // Phase A.3 echo
}

TEST_CASE("ADR-019 §6.6 Hero example — placed box at (110, 360, 1700, 360)") {
    // §6.6 Hero example: layer.text("title").place(TextPlacement::CanvasCenter)
    // on canvas 1920×1080 with box 1700×360 and anchor Center (the default).
    // Expected: pin (960, 540) → box_top_left (110, 360); local_frame
    // {110, 360, 1700, 360}; world_matrix = T(110, 360, 0) for top-level
    // layer (identity parent).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.local_frame.x == doctest::Approx(110.0f));   // §6.6 box_top_left.x
    CHECK(r.local_frame.y == doctest::Approx(360.0f));   // §6.6 box_top_left.y
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // §6.6 box width
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // §6.6 box height
    CHECK(mat4_near(r.layer_matrix, Mat4(1.0f)));        // top-level layer → identity
    Mat4 expected_world = glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected_world));    // §6.6 world_matrix = T(110, 360, 0)
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));
}
