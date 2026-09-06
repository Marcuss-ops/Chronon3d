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
//
// This file was split to keep every part ≤ 400 LOC.  Split parts:
//   - test_text_placement_resolver_anchors.cpp        (non-default anchors …)
//   - test_text_placement_resolver_adr019.cpp         (Phase A.3 + ADR-019 §6)
//   - test_text_placement_resolver_coordinate_contract.cpp  (CoordinateContract)

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement.hpp>           // TextPlacement, TextPlacementKind
#include <chronon3d/text/resolve_text_placement.hpp>    // Canonical resolver surface
#include <chronon3d/layout/layout_solver.hpp>            // Canvas-space layer pin math
#include <chronon3d/scene/builders/layer_builder.hpp>    // Materialized TextRun transform contract
#include <chronon3d/scene/model/core/scene.hpp>         // Rasterizer-independent layer contract

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
