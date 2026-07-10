// test_text_placement_resolver.cpp — Tests for the unified text placement resolver
//
// ADR-019 Decision 3: TextPlacement resolves the Box coordinate level.
// Tests verify that every placement variant produces the correct origin
// and world_matrix for a 1920×1080 canvas.

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

// Default 16:9 canvas with standard safe margins.
static CanvasInfo default_canvas() {
    return CanvasInfo{1920.0f, 1080.0f, 54.0f, 54.0f, 96.0f, 96.0f};
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: resolve_placement_origin — all 12 variants
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("PlacementOrigin: CanvasCenter") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::CanvasCenter);
    // center: (1920-1700)/2 = 110, (1080-360)/2 = 360
    CHECK(vec2_near(o, {110.0f, 360.0f}));
}

TEST_CASE("PlacementOrigin: TopLeft") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::TopLeft);
    CHECK(vec2_near(o, {96.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopCenter") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::TopCenter);
    // x = (1920-800)/2 = 560, y = safe_margin_top = 54
    CHECK(vec2_near(o, {560.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: TopRight") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::TopRight);
    // x = 1920 - 800 - 96 = 1024, y = 54
    CHECK(vec2_near(o, {1024.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: CenterLeft") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::CenterLeft);
    // x = 96, y = (1080-200)/2 = 440
    CHECK(vec2_near(o, {96.0f, 440.0f}));
}

TEST_CASE("PlacementOrigin: CenterRight") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::CenterRight);
    // x = 1920 - 800 - 96 = 1024, y = 440
    CHECK(vec2_near(o, {1024.0f, 440.0f}));
}

TEST_CASE("PlacementOrigin: BottomLeft") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::BottomLeft);
    // x = 96, y = 1080 - 200 - 54 = 826
    CHECK(vec2_near(o, {96.0f, 826.0f}));
}

TEST_CASE("PlacementOrigin: BottomCenter") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::BottomCenter);
    // x = (1920-800)/2 = 560, y = 1080 - 200 - 54 = 826
    CHECK(vec2_near(o, {560.0f, 826.0f}));
}

TEST_CASE("PlacementOrigin: BottomRight") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::BottomRight);
    // x = 1920 - 800 - 96 = 1024, y = 826
    CHECK(vec2_near(o, {1024.0f, 826.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaTop") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::SafeAreaTop);
    // x = (1920-1200)/2 = 360, y = 54
    CHECK(vec2_near(o, {360.0f, 54.0f}));
}

TEST_CASE("PlacementOrigin: SafeAreaBottom") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 100.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::SafeAreaBottom);
    // x = (1920-1200)/2 = 360, y = 1080 - 100 - 54 = 926
    CHECK(vec2_near(o, {360.0f, 926.0f}));
}

TEST_CASE("PlacementOrigin: Absolute") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto o = resolve_placement_origin(c, box, TextPlacement::Absolute, {500.0f, 300.0f});
    // Absolute: origin = offset directly (no canvas math)
    CHECK(vec2_near(o, {500.0f, 300.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: resolve_placement_origin — offset is additive
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("PlacementOrigin: offset is additive for non-Absolute") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    // TopLeft + offset(10, 20) = (96+10, 54+20) = (106, 74)
    auto o = resolve_placement_origin(c, box, TextPlacement::TopLeft, {10.0f, 20.0f});
    CHECK(vec2_near(o, {106.0f, 74.0f}));
}

TEST_CASE("PlacementOrigin: CanvasCenter with negative offset") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    // CanvasCenter(110, 360) + offset(0, -100) = (110, 260)
    auto o = resolve_placement_origin(c, box, TextPlacement::CanvasCenter, {0.0f, -100.0f});
    CHECK(vec2_near(o, {110.0f, 260.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: resolve_text_placement — default anchor (Center)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: CanvasCenter default anchor — 1920×1080") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter);

    // local_frame = (110, 360, 1700, 360)
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(360.0f));
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));
    CHECK(r.local_frame.w == doctest::Approx(360.0f));

    // layout_origin = (110, 360) — top-left of the box
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));

    // layer_matrix = identity (no parent transform)
    CHECK(mat4_near(r.layer_matrix, Mat4(1.0f)));

    // world_matrix = T(110, 360, 0)
    Mat4 expected = glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
}

TEST_CASE("TextPlacement: SafeAreaBottom default anchor") {
    auto c = default_canvas();
    Vec2 box{1200.0f, 80.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::SafeAreaBottom);

    // x = (1920-1200)/2 = 360, y = 1080 - 80 - 54 = 946
    CHECK(r.local_frame.x == doctest::Approx(360.0f));
    CHECK(r.local_frame.y == doctest::Approx(946.0f));
    CHECK(vec2_near(r.layout_origin, {360.0f, 946.0f}));
}

TEST_CASE("TextPlacement: Absolute with explicit position") {
    auto c = default_canvas();
    Vec2 box{400.0f, 100.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::Absolute, {500.0f, 300.0f});

    CHECK(r.local_frame.x == doctest::Approx(500.0f));
    CHECK(r.local_frame.y == doctest::Approx(300.0f));
    CHECK(vec2_near(r.layout_origin, {500.0f, 300.0f}));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: resolve_text_placement — non-default anchors (code reviewer fix)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: CanvasCenter + TextAnchor::TopLeft shifts box") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    // With TopLeft anchor, the box's TOP-LEFT aligns with canvas center.
    // So box_origin = (960, 540) — not (110, 360).
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {},
                                     TextAnchor::TopLeft);

    // adjustment = center_anchor - TopLeft_anchor = (850, 180) - (0, 0) = (850, 180)
    // origin = default_origin(110, 360) + adjust(850, 180) = (960, 540)
    CHECK(r.local_frame.x == doctest::Approx(960.0f));
    CHECK(r.local_frame.y == doctest::Approx(540.0f));
    CHECK(vec2_near(r.layout_origin, {960.0f, 540.0f}));
}

TEST_CASE("TextPlacement: CanvasCenter + TextAnchor::BottomCenter shifts box up") {
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    // With BottomCenter anchor, the box's BOTTOM-CENTER aligns with canvas center.
    // anchor = (box.x/2, box.y) = (850, 360)
    // adjustment = center(850, 180) - bottom_center(850, 360) = (0, -180)
    // origin = (110, 360) + (0, -180) = (110, 180)
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter, {},
                                     TextAnchor::BottomCenter);
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(180.0f));
    CHECK(vec2_near(r.layout_origin, {110.0f, 180.0f}));
}

TEST_CASE("TextPlacement: TopLeft + TextAnchor::TopLeft — no shift") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    // TopLeft placement + TopLeft anchor = box at safe margins, no adjustment.
    // adjust = center(400, 100) - TopLeft(0, 0) = (400, 100)
    // default_origin = (96, 54)
    // origin = (96+400, 54+100) = (496, 154) — box shifted so top-left is at safe margin
    auto r = resolve_text_placement(c, box, TextPlacement::TopLeft, {},
                                     TextAnchor::TopLeft);
    CHECK(r.local_frame.x == doctest::Approx(496.0f));
    CHECK(r.local_frame.y == doctest::Approx(154.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: resolve_text_placement — with parent layer_matrix
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: with layer_matrix (parent transform)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    Mat4 parent = glm::translate(Mat4(1.0f), Vec3(100.0f, 50.0f, 0.0f));
    auto r = resolve_text_placement(c, box, TextPlacement::TopLeft, {},
                                     TextAnchor::Center, parent);

    // Default anchor (Center): adjust = (0, 0), origin = (96, 54) + (400, 100) = (496, 154)
    // world_matrix = parent × T(496, 154, 0)
    Mat4 expected = parent * glm::translate(Mat4(1.0f), Vec3(496.0f, 154.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
    CHECK(mat4_near(r.layer_matrix, parent));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: 9:16 portrait canvas
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: 9:16 portrait — CanvasCenter") {
    CanvasInfo portrait{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
    Vec2 box{900.0f, 200.0f};
    auto r = resolve_text_placement(portrait, box, TextPlacement::CanvasCenter);

    // (1080-900)/2 = 90, (1920-200)/2 = 860
    CHECK(r.local_frame.x == doctest::Approx(90.0f));
    CHECK(r.local_frame.y == doctest::Approx(860.0f));
    CHECK(vec2_near(r.layout_origin, {90.0f, 860.0f}));
}

TEST_CASE("TextPlacement: 9:16 portrait — SafeAreaBottom") {
    CanvasInfo portrait{1080.0f, 1920.0f, 96.0f, 96.0f, 54.0f, 54.0f};
    Vec2 box{900.0f, 120.0f};
    auto r = resolve_text_placement(portrait, box, TextPlacement::SafeAreaBottom);

    // x = (1080-900)/2 = 90, y = 1920 - 120 - 96 = 1704
    CHECK(r.local_frame.x == doctest::Approx(90.0f));
    CHECK(r.local_frame.y == doctest::Approx(1704.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: zero-size box (edge case)
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: zero-size box") {
    auto c = default_canvas();
    Vec2 box{0.0f, 0.0f};
    auto r = resolve_text_placement(c, box, TextPlacement::CanvasCenter);

    // (1920-0)/2 = 960, (1080-0)/2 = 540
    CHECK(r.local_frame.x == doctest::Approx(960.0f));
    CHECK(r.local_frame.y == doctest::Approx(540.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: world_matrix transforms a box-local point to canvas coords
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("TextPlacement: world_matrix transforms origin correctly") {
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
// TEST_CASE: all placements produce non-degenerate matrices
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

        // determinant must be non-zero (valid transform)
        f32 det = glm::determinant(r.world_matrix);
        CHECK(std::abs(det) > 0.001f);

        // layout_origin must be finite
        CHECK(std::isfinite(r.layout_origin.x));
        CHECK(std::isfinite(r.layout_origin.y));

        // local_frame dimensions must match box_size
        CHECK(r.local_frame.z == doctest::Approx(box.x));
        CHECK(r.local_frame.w == doctest::Approx(box.y));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// TEST_CASE: determinism — same input produces same output
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
