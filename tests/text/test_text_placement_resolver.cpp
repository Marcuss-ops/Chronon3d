// test_text_placement_resolver.cpp — Tests for the unified text placement resolver
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level.
//
// Semantics:
//   resolve_placement_origin() returns the PIN POINT — where the box's
//   anchor point should sit on the canvas.  It is NOT the box top-left.
//   resolve_text_placement() computes box_top_left = pin_point - anchor_offset.

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement_resolver.hpp>

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
    auto pin = resolve_placement_origin(c, box, TextPlacement::CanvasCenter);
    // pin point = canvas center = (960, 540)
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: TopLeft pin = safe margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::TopLeft);
    CHECK(vec2_near(pin, {96.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopCenter pin = top-center") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::TopCenter);
    // x = canvas center = 960, y = safe_margin_top = 54
    CHECK(vec2_near(pin, {960.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopRight pin = top-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::TopRight);
    // x = 1920 - 96 = 1824, y = 54
    CHECK(vec2_near(pin, {1824.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: CenterLeft pin = center-left") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::CenterLeft);
    // x = 96, y = 540
    CHECK(vec2_near(pin, {96.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: CenterRight pin = center-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::CenterRight);
    // x = 1920 - 96 = 1824, y = 540
    CHECK(vec2_near(pin, {1824.0f, 540.0f}));
}

TEST_CASE("PlacementOrigin: BottomLeft pin = bottom-left minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::BottomLeft);
    // x = 96, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {96.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: BottomCenter pin = bottom-center minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::BottomCenter);
    // x = 960, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {960.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: BottomRight pin = bottom-right minus margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::BottomRight);
    // x = 1920 - 96 = 1824, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {1824.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaTop pin = top-center") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::SafeAreaTop);
    // x = 960, y = 54
    CHECK(vec2_near(pin, {960.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaBottom pin = bottom-center") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::SafeAreaBottom);
    // x = 960, y = 1080 - 54 = 1026
    CHECK(vec2_near(pin, {960.0f, 1026.0f}));
}

TEST_CASE("PlacementOrigin: Absolute pin = offset") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement::Absolute, {500.0f, 300.0f});
    CHECK(vec2_near(pin, {500.0f, 300.0f}));
}

TEST_CASE("PlacementOrigin: offset is additive for non-Absolute") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    // TopLeft pin(96, 54) + offset(10, 20) = (106, 74)
    auto pin = resolve_placement_origin(c, box, TextPlacement::TopLeft, {10.0f, 20.0f});
    CHECK(vec2_near(pin, {106.0f, 74.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// resolve_text_placement — default anchor (Center)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: CanvasCenter + Center anchor — classic centered title") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter);

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
    auto r = resolve_text_placement(c, box, TextPlacement::SafeAreaBottom);

    // pin = (960, 1026), anchor = (600, 40), origin = (360, 986)
    CHECK(r.local_frame.x == doctest::Approx(360.0f));
    CHECK(r.local_frame.y == doctest::Approx(986.0f));
    CHECK(vec2_near(r.layout_origin, {360.0f, 986.0f}));
}

TEST_CASE("TextPlacement: Absolute + Center anchor") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::Absolute, {500.0f, 300.0f});

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
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {},
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
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {},
                                     TextAnchor::BottomCenter);

    // pin = (960, 540), anchor = (850, 360), origin = (110, 180)
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(180.0f));
    CHECK(vec2_near(r.layout_origin, {110.0f, 180.0f}));
}

TEST_CASE("TextPlacement: TopLeft + TopLeft anchor — box at safe margin") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::TopLeft, {},
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
    auto r = resolve_text_placement(c, box, TextPlacement::TopLeft, {},
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
    auto r = resolve_text_placement(c, box, TextPlacement::TopLeft, {},
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
    auto r = resolve_text_placement(portrait, box, TextPlacement::CanvasCenter);

    // pin = (540, 960), anchor = (450, 100), origin = (90, 860)
    CHECK(r.local_frame.x == doctest::Approx(90.0f));
    CHECK(r.local_frame.y == doctest::Approx(860.0f));
    CHECK(vec2_near(r.layout_origin, {90.0f, 860.0f}));
}

TEST_CASE("TextPlacement: 9:16 portrait — SafeAreaBottom") {
    CanvasInfo portrait{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
    Vec2 box{900.0f, 120.0f};
    auto r = resolve_text_placement(portrait, box, TextPlacement::SafeAreaBottom);

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
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter);

    // pin = (960, 540), anchor = (0, 0), origin = (960, 540)
    CHECK(r.local_frame.x == doctest::Approx(960.0f));
    CHECK(r.local_frame.y == doctest::Approx(540.0f));
}

TEST_CASE("TextPlacement: world_matrix transforms box-local origin correctly") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter);

    // Box-local (0,0,0,1) should map to canvas (110, 360, 0, 1)
    Vec4 local_origin = r.world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(local_origin.x == doctest::Approx(110.0f));
    CHECK(local_origin.y == doctest::Approx(360.0f));
    CHECK(local_origin.z == doctest::Approx(0.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// All placements produce valid matrices
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: all variants produce valid world_matrix") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};

    TextPlacement variants[] = {
        TextPlacement::CanvasCenter,
        TextPlacement::TopLeft,
        TextPlacement::TopCenter,
        TextPlacement::TopRight,
        TextPlacement::CenterLeft,
        TextPlacement::CenterRight,
        TextPlacement::BottomLeft,
        TextPlacement::BottomCenter,
        TextPlacement::BottomRight,
        TextPlacement::SafeAreaTop,
        TextPlacement::SafeAreaBottom,
        TextPlacement::Absolute,
    };

    for (auto p : variants) {
        Vec2 off = (p == TextPlacement::Absolute) ? Vec2{500.0f, 300.0f} : Vec2{0.0f};
        auto r = resolve_text_placement(c, box, p, off);

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
    auto first = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {10.0f, -20.0f},
                                         TextAnchor::Center);
    for (int i = 0; i < 100; ++i) {
        auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {10.0f, -20.0f},
                                         TextAnchor::Center);
        CHECK(mat4_near(r.world_matrix, first.world_matrix));
        CHECK(vec2_near(r.layout_origin, first.layout_origin));
    }
}
