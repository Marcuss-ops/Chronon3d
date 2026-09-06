// test_text_placement_resolver_anchors.cpp — Split from
// test_text_placement_resolver.cpp: non-default anchors, parent
// layer_matrix composition, 9:16 portrait, edge cases, all-variants
// matrix validity, determinism and the Phase A.2 struct-API contract.
//
// Semantics:
//   resolve_placement_origin() returns the PIN POINT — where the box's
//   anchor point should sit on the canvas.  It is NOT the box top-left.
//   resolve_text_placement() computes box_top_left = pin_point - anchor_offset.

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
