#include <doctest/doctest.h>
#include <chronon3d/layout/layout_solver.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/layer/layer.hpp>

using namespace chronon3d;

static Layer make_layer(Vec3 pos, LayoutRules rules = {}) {
    Layer l;
    l.transform.position = pos;
    l.layout = rules;
    return l;
}

// ---------------------------------------------------------------------------
// anchor_position helper
// ---------------------------------------------------------------------------
TEST_CASE("LayoutSolver: anchor_position TopLeft + margin") {
    Vec2 p = anchor_position(Anchor::TopLeft, 1280, 720, 20.0f);
    CHECK(p.x == doctest::Approx(20.0f));
    CHECK(p.y == doctest::Approx(20.0f));
}

TEST_CASE("LayoutSolver: anchor_position Center") {
    Vec2 p = anchor_position(Anchor::Center, 1280, 720, 0.0f);
    CHECK(p.x == doctest::Approx(640.0f));
    CHECK(p.y == doctest::Approx(360.0f));
}

TEST_CASE("LayoutSolver: anchor_position BottomCenter with margin") {
    Vec2 p = anchor_position(Anchor::BottomCenter, 1280, 720, 48.0f);
    CHECK(p.x == doctest::Approx(640.0f));
    CHECK(p.y == doctest::Approx(672.0f));
}

TEST_CASE("LayoutSolver: anchor_position TopCenter with offset") {
    Vec3 p = anchor_position(Anchors::TopCenter.offset_by({0.0f, 80.0f}), 1280, 720, 0.0f);
    CHECK(p.x == doctest::Approx(640.0f));
    CHECK(p.y == doctest::Approx(80.0f));
    CHECK(p.z == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Layout rules not enabled -- no change
// ---------------------------------------------------------------------------
TEST_CASE("LayoutSolver: disabled rules leave position unchanged") {
    Scene scene;
    Layer l = make_layer({100, 200, 0}, LayoutRules{.enabled=false});
    scene.add_layer(std::move(l));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(100.0f));
    CHECK(scene.layers()[0].transform.position.y == doctest::Approx(200.0f));
}

// ---------------------------------------------------------------------------
// Pin to anchor
// ---------------------------------------------------------------------------
TEST_CASE("LayoutSolver: pin to BottomCenter sets position") {
    Scene scene;
    LayoutRules rules;
    rules.enabled = true;
    rules.pin     = AnchorPlacement{Anchor::BottomCenter};
    rules.margin  = 40.0f;
    scene.add_layer(make_layer({0, 0, 0}, rules));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(640.0f));
    CHECK(scene.layers()[0].transform.position.y == doctest::Approx(680.0f));
}

TEST_CASE("LayoutSolver: pin to TopRight with margin") {
    Scene scene;
    LayoutRules rules;
    rules.enabled = true;
    rules.pin     = AnchorPlacement{Anchor::TopRight};
    rules.margin  = 20.0f;
    scene.add_layer(make_layer({0, 0, 0}, rules));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(1260.0f));
    CHECK(scene.layers()[0].transform.position.y == doctest::Approx(20.0f));
}

TEST_CASE("LayoutSolver: pin offset and depth are applied") {
    Scene scene;
    LayoutRules rules;
    rules.enabled = true;
    rules.pin = Anchors::Center.offset_by({0.0f, 80.0f}).with_depth(-300.0f);
    scene.add_layer(make_layer({0, 0, 0}, rules));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    const Vec3& p = scene.layers()[0].transform.position;
    CHECK(p.x == doctest::Approx(640.0f));
    CHECK(p.y == doctest::Approx(440.0f));
    CHECK(p.z == doctest::Approx(-300.0f));
}

// ---------------------------------------------------------------------------
// Safe area clamp
// ---------------------------------------------------------------------------
TEST_CASE("LayoutSolver: keep_in_safe_area clamps out-of-bounds position") {
    Scene scene;
    LayoutRules rules;
    rules.enabled           = true;
    rules.keep_in_safe_area = true;
    rules.safe_area         = SafeArea{.top=40, .bottom=40, .left=40, .right=40};
    scene.add_layer(make_layer({-50, 800, 0}, rules));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    const Vec3& p = scene.layers()[0].transform.position;
    CHECK(p.x >= 40.0f);
    CHECK(p.y <= 680.0f);  // 720 - 40
}

// ---------------------------------------------------------------------------
// fit_text enables auto_scale on text nodes
// ---------------------------------------------------------------------------
TEST_CASE("LayoutSolver: fit_text enables auto_scale on text nodes") {
    Scene scene;
    Layer l;
    l.layout.enabled  = true;
    l.layout.fit_text = true;

    RenderNode node;
    node.shape.type           = ShapeType::Text;
    node.shape.text.text      = "Hello";
    node.shape.text.style.size = 48.0f;
    node.shape.text.style.auto_scale = false;
    l.nodes.push_back(std::move(node));
    scene.add_layer(std::move(l));

    LayoutSolver solver;
    solver.solve(scene, 1280, 720);

    CHECK(scene.layers()[0].nodes[0].shape.text.style.auto_scale);
    CHECK(scene.layers()[0].nodes[0].shape.text.box.enabled);
}
