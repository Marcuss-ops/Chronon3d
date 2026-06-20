// tests/layout/test_layout_flow_grid.cpp
//
// Tests for `LayoutFlow` / `LayoutGrid` containers added by the layout
// solver extension. The new pass groups layers by `group_id` and lays
// them out as a Flex-style linear or uniform-grid container while the
// existing Pin / SafeArea passes continue to operate on non-grouped
// layers.
//
// These tests construct `Scene` and `Layer` directly with std::pmr and
// invoke `LayoutSolver::solve` — they do not depend on the renderer.

#include <doctest/doctest.h>

#include <chronon3d/layout/layout_solver.hpp>
#include <chronon3d/layout/layout_flow_grid.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>

#include <memory_resource>

using namespace chronon3d;

// Helper: build a fresh scene with one enabled, optionally-grouped layer.
static void push_layer(Scene& scene,
                      const std::string& name,
                      Vec3 initial_pos = {0.0f, 0.0f, 0.0f}) {
    auto& layers = scene.layers();
    Layer layer{};
    layer.name = name;
    layer.layout.enabled = true;
    layer.transform.position = initial_pos;
    layers.push_back(layer);
}

// Helper: extract the layer with the given name (assumes ordering known).
// `std::pmr::string` and `std::string` are different `std::basic_string`
// specialisations (different allocators) and have no cross-operator==, so
// we compare against `std::string(layer.name)` explicitly.
static Layer& find_layer(Scene& scene, const std::string& name) {
    for (auto& layer : scene.layers()) {
        if (std::string(layer.name) == name) return layer;
    }
    // Test bug — fail loudly
    throw std::runtime_error("layer not found: " + name);
}

// ──────────────────────────────────────────────────────────────────────
//  LayoutFlow — row
// ──────────────────────────────────────────────────────────────────────

TEST_CASE("LayoutFlow row: 3 items with gap=10 are evenly spaced") {
    Scene scene;
    push_layer(scene, "a");
    push_layer(scene, "b");
    push_layer(scene, "c");

    find_layer(scene, "a").layout.flow = LayoutFlow{
        .main_axis = Axis::Row,
        .gap = 10.0f,
        .align = AlignItems::Start,
        .group_id = "row-a",
        .cell_size = Vec2{100.0f, 50.0f},
    };
    find_layer(scene, "b").layout.flow = LayoutFlow{
        .main_axis = Axis::Row, .gap = 10.0f,
        .align = AlignItems::Start, .group_id = "row-a",
        .cell_size = Vec2{100.0f, 50.0f},
    };
    find_layer(scene, "c").layout.flow = LayoutFlow{
        .main_axis = Axis::Row, .gap = 10.0f,
        .align = AlignItems::Start, .group_id = "row-a",
        .cell_size = Vec2{100.0f, 50.0f},
    };

    LayoutSolver{}.solve(scene, /*w=*/600, /*h=*/400);

    // Each cell is 100px wide + 10px gap. Center-x: 50, 160, 270.
    // Cross-axis (Y, Start): center = 25 (half of 50).
    CHECK(find_layer(scene, "a").transform.position.x == doctest::Approx(50.0f));
    CHECK(find_layer(scene, "a").transform.position.y == doctest::Approx(25.0f));
    CHECK(find_layer(scene, "b").transform.position.x == doctest::Approx(160.0f));
    CHECK(find_layer(scene, "b").transform.position.y == doctest::Approx(25.0f));
    CHECK(find_layer(scene, "c").transform.position.x == doctest::Approx(270.0f));
    CHECK(find_layer(scene, "c").transform.position.y == doctest::Approx(25.0f));
}

TEST_CASE("LayoutFlow column: items stack vertically with gap") {
    Scene scene;
    push_layer(scene, "x");
    push_layer(scene, "y");

    for (auto& layer : scene.layers()) {
        layer.layout.flow = LayoutFlow{
            .main_axis = Axis::Column,
            .gap = 20.0f,
            .align = AlignItems::Start,
            .group_id = "col",
            .cell_size = Vec2{80.0f, 60.0f},
        };
    }

    LayoutSolver{}.solve(scene, 400, 600);

    // Y centers: 30 (top), 110 (60 + 20 + 30).
    // X (Start, height 600): 40 (half of 80).
    CHECK(find_layer(scene, "x").transform.position.x == doctest::Approx(40.0f));
    CHECK(find_layer(scene, "x").transform.position.y == doctest::Approx(30.0f));
    CHECK(find_layer(scene, "y").transform.position.x == doctest::Approx(40.0f));
    CHECK(find_layer(scene, "y").transform.position.y == doctest::Approx(110.0f));
}

TEST_CASE("LayoutFlow align=Center: cross-axis is centered") {
    Scene scene;
    push_layer(scene, "only");

    // Cell size zero ⇒ fallback divides canvas by member count ⇒ 400x600.
    find_layer(scene, "only").layout.flow = LayoutFlow{
        .main_axis = Axis::Row, .gap = 0.0f,
        .align = AlignItems::Center,
        .group_id = "ctr",
        .cell_size = Vec2{0.0f, 0.0f},  // auto
    };

    LayoutSolver{}.solve(scene, 400, 600);

    // Only member of "ctr" → cell auto = (400/1, 600/1) = (400, 600).
    // Main axis (Row) center-x = 200, cross-axis (Y) center with Align::Center.
    CHECK(find_layer(scene, "only").transform.position.x == doctest::Approx(200.0f));
    // Cross-axis Center ⇒ (canvas - extent) / 2 ⇒ (600 - 600) / 2 = 0, + half-cell.
    CHECK(find_layer(scene, "only").transform.position.y == doctest::Approx(300.0f));
}

TEST_CASE("LayoutFlow wrap=true: items that overflow wrap onto secondary axis") {
    Scene scene;
    push_layer(scene, "w1");
    push_layer(scene, "w2");
    push_layer(scene, "w3");

    // Each cell 80 wide, gap 0. Canvas main axis (Row) = 200.
    // Cell 1: x=40;  Cell 2: x=40+80=120 (still fits within 200).
    // Cell 3: would exceed ⇒ wrap.
    for (auto& layer : scene.layers()) {
        layer.layout.flow = LayoutFlow{
            .main_axis = Axis::Row, .gap = 0.0f,
            .wrap = true,
            .align = AlignItems::Start,
            .group_id = "wrap",
            .cell_size = Vec2{80.0f, 50.0f},
        };
    }

    LayoutSolver{}.solve(scene, 200, 300);

    CHECK(find_layer(scene, "w1").transform.position.x == doctest::Approx(40.0f));
    CHECK(find_layer(scene, "w1").transform.position.y == doctest::Approx(25.0f));

    CHECK(find_layer(scene, "w2").transform.position.x == doctest::Approx(120.0f));
    CHECK(find_layer(scene, "w2").transform.position.y == doctest::Approx(25.0f));

    // Wraps: top of row 2 starts where row 1 ended vertically.
    CHECK(find_layer(scene, "w3").transform.position.x == doctest::Approx(40.0f));
    CHECK(find_layer(scene, "w3").transform.position.y == doctest::Approx(75.0f));  // 25 + 50
}

// ──────────────────────────────────────────────────────────────────────
//  LayoutGrid — row-major
// ──────────────────────────────────────────────────────────────────────

TEST_CASE("LayoutGrid 2x2 with gap=10 places items row-major") {
    Scene scene;
    push_layer(scene, "g0");
    push_layer(scene, "g1");
    push_layer(scene, "g2");
    push_layer(scene, "g3");

    for (auto& layer : scene.layers()) {
        layer.layout.grid = LayoutGrid{
            .columns = 2, .rows = 2,
            .gap = Vec2{10.0f, 10.0f},
            .fit = GridFit::Auto,
            .group_id = "g",
            .cell_size = Vec2{100.0f, 80.0f},
        };
    }

    LayoutSolver{}.solve(scene, 1000, 1000);

    // Centers are computed as (c + 0.5)*(W + gap_x), (r + 0.5)*(H + gap_y).
    // Cell (100, 80), gap (10, 10).
    // g0: (0,0) → (50, 40)
    // g1: (1,0) → (160, 40)
    // g2: (0,1) → (50, 130)
    // g3: (1,1) → (160, 130)
    auto& l0 = find_layer(scene, "g0");
    auto& l1 = find_layer(scene, "g1");
    auto& l2 = find_layer(scene, "g2");
    auto& l3 = find_layer(scene, "g3");

    CHECK(l0.transform.position.x == doctest::Approx(50.0f));
    CHECK(l0.transform.position.y == doctest::Approx(40.0f));
    CHECK(l1.transform.position.x == doctest::Approx(160.0f));
    CHECK(l1.transform.position.y == doctest::Approx(40.0f));
    CHECK(l2.transform.position.x == doctest::Approx(50.0f));
    CHECK(l2.transform.position.y == doctest::Approx(130.0f));
    CHECK(l3.transform.position.x == doctest::Approx(160.0f));
    CHECK(l3.transform.position.y == doctest::Approx(130.0f));
}

TEST_CASE("LayoutGrid FixedRows expands columns when items exceed rows*cols") {
    Scene scene;
    push_layer(scene, "fr0");
    push_layer(scene, "fr1");
    push_layer(scene, "fr2");
    push_layer(scene, "fr3");
    push_layer(scene, "fr4");

    // 5 items, rows=2 fixed → effective_cols = ceil(5/2) = 3.
    for (auto& layer : scene.layers()) {
        layer.layout.grid = LayoutGrid{
            .columns = 1, .rows = 2,
            .gap = Vec2{0.0f, 0.0f},
            .fit = GridFit::FixedRows,
            .group_id = "fr",
            .cell_size = Vec2{50.0f, 50.0f},
        };
    }

    LayoutSolver{}.solve(scene, 1000, 1000);

    // Layout: rows=2, columns=3 (4th item wraps to row 0 col 3 — but only 3 cols
    // ⇒ goes to row 1 col 0, and 5th goes to row 1 col 1).
    // g0: r=0,c=0 → (25, 25)
    // g1: r=0,c=1 → (75, 25)
    // g2: r=0,c=2 → (125, 25)
    // g3: r=1,c=0 → (25, 75)
    // g4: r=1,c=1 → (75, 75)
    CHECK(find_layer(scene, "fr0").transform.position.x == doctest::Approx(25.0f));
    CHECK(find_layer(scene, "fr2").transform.position.x == doctest::Approx(125.0f));
    CHECK(find_layer(scene, "fr3").transform.position.y == doctest::Approx(75.0f));
}

TEST_CASE("LayoutGrid: columns=0 is a defensive no-op") {
    Scene scene;
    push_layer(scene, "zero");

    const Vec3 initial = {7.0f, 11.0f, 13.0f};
    find_layer(scene, "zero").transform.position = initial;
    find_layer(scene, "zero").layout.grid = LayoutGrid{
        .columns = 0, .rows = 1, .group_id = "nz",
    };

    LayoutSolver{}.solve(scene, 800, 600);

    // Pass is a no-op → position untouched.
    CHECK(find_layer(scene, "zero").transform.position.x == doctest::Approx(7.0f));
    CHECK(find_layer(scene, "zero").transform.position.y == doctest::Approx(11.0f));
    CHECK(find_layer(scene, "zero").transform.position.z == doctest::Approx(13.0f));
}

// ──────────────────────────────────────────────────────────────────────
//  Integration: Pin skipped for grouped layers
// ──────────────────────────────────────────────────────────────────────

TEST_CASE("LayoutSolver: layer in a Flow is NOT touched by the Pin pass") {
    Scene scene;
    push_layer(scene, "flowed");

    find_layer(scene, "flowed").layout.flow = LayoutFlow{
        .main_axis = Axis::Row, .group_id = "f",
        .cell_size = Vec2{100.0f, 50.0f},
    };
    // Pin is set, but solver should skip it for grouped layers.
    find_layer(scene, "flowed").layout.pin = Anchors::Center;

    LayoutSolver{}.solve(scene, 800, 600);

    // Flow placed at (50, 25) — the cell declared as (100, 50) is centered at
    // (100/2, 50/2) = (50, 25) along the Row main axis. Pin is skipped because
    // the layer belongs to a Flow group; if Pin were NOT skipped, the position
    // would have grown to ~(450, 325) (Pin offset = canvas center 400/300 +
    // Flow placement 50/25).
    CHECK(find_layer(scene, "flowed").transform.position.x == doctest::Approx(50.0f));
    CHECK(find_layer(scene, "flowed").transform.position.y == doctest::Approx(25.0f));
}

TEST_CASE("LayoutSolver: layer with both flow and grid is treated as Mixed (no-op)") {
    Scene scene;
    push_layer(scene, "both", Vec3{1.0f, 2.0f, 3.0f});

    auto& l = find_layer(scene, "both");
    l.layout.flow = LayoutFlow{ .main_axis = Axis::Row, .group_id = "x" };
    l.layout.grid = LayoutGrid{ .columns = 2, .rows = 2, .group_id = "x" };

    LayoutSolver{}.solve(scene, 800, 600);

    // Mixed group is skipped; Pin not set; SafeArea off → position unchanged.
    CHECK(l.transform.position.x == doctest::Approx(1.0f));
    CHECK(l.transform.position.y == doctest::Approx(2.0f));
}

TEST_CASE("LayoutSolver: Pin still applies to NON-grouped layers") {
    Scene scene;
    push_layer(scene, "pinned", Vec3{0.0f, 0.0f, 0.0f});
    push_layer(scene, "grouped");

    find_layer(scene, "pinned").layout.pin = Anchors::Center;
    find_layer(scene, "grouped").layout.flow = LayoutFlow{
        .main_axis = Axis::Row, .group_id = "g",
        .cell_size = Vec2{100.0f, 50.0f},
    };

    LayoutSolver{}.solve(scene, 800, 600);

    // pinned: 0 + (W/2, H/2) = (400, 300).
    auto& p = find_layer(scene, "pinned");
    CHECK(p.transform.position.x == doctest::Approx(400.0f));
    CHECK(p.transform.position.y == doctest::Approx(300.0f));

    // grouped: Flow positions it at (50, 25).
    auto& g = find_layer(scene, "grouped");
    CHECK(g.transform.position.x == doctest::Approx(50.0f));
    CHECK(g.transform.position.y == doctest::Approx(25.0f));
}
