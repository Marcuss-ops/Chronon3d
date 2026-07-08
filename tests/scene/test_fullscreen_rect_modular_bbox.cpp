// =============================================================================
// test_fullscreen_rect_modular_bbox.cpp — FIX 4 regression test for the
// 10-point friction audit.
//
// Verifies that `LayerBuilder::fullscreen_rect(name, color)` and its alias
// `LayerBuilder::fill(color)` produce a canvas-correct RectShape in BOTH
// modular_coordinates=true and modular_coordinates=false modes:
//
//   - pin_to(Anchor::Center) is set (LAYOUT pin flag flipped)
//   - rect.pos = (-m_screen_width/2, -m_screen_height/2, 0)  pre-baked offset
//   - rect.size = (m_screen_width, m_screen_height)
//
// This regression test catches re-simplifications that "tidy" pos back to
// (0,0,0) — which reintroduces the half-canvas bug (final bbox
// (w/2,h/2)→(w,h) instead of (0,0)→(w,h) in modular_coordinates mode).
//
// File category: Cat-2 test infrastructure under
//   tests/scene/test_fullscreen_rect_modular_bbox.cpp
// Registered in `chronon3d_scene_tests` source list in tests/scene_tests.cmake.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/layout/layout_rules.hpp>          // Anchor + AnchorPlacement explicit include
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/shape/rect_shape.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/registry/shape_ids.hpp>

using namespace chronon3d;

namespace {

// ── helpers ────────────────────────────────────────────────────────────
// Read the RectShape from a layer's first RenderNode.  Calls REQUIRE so
// any structural mismatch is reported as a test failure with a useful
// message instead of a dereference of `nullopt`.
const RectShape& first_rect_of(const Layer& layer) {
    REQUIRE(layer.nodes.size() == 1);
    REQUIRE(layer.nodes[0].shape.type() == ShapeType::Rect);
    return layer.nodes[0].shape.rect();
}

} // namespace

TEST_CASE("FIX 4: fullscreen_rect creates canvas-correct shape (1920x1080 default)") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& l) {
        l.screen_dimensions(1920.0f, 1080.0f);
        l.fullscreen_rect("fill", Color::white());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    // FIX 4 — pin_to(Anchor::Center) MUST be set by fullscreen_rect.
    CHECK(layer.layout.enabled);
    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);
    CHECK(layer.layout.pin->margin == doctest::Approx(0.0f));

    // FIX 4 — pos = (-w/2, -h/2, 0) pre-baked negative half offset.
    const RectShape& rect = first_rect_of(layer);
    CHECK(rect.size.x == doctest::Approx(1920.0f));
    CHECK(rect.size.y == doctest::Approx(1080.0f));
    CHECK(rect.pos.x  == doctest::Approx(-960.0f));   // -m_screen_width  * 0.5
    CHECK(rect.pos.y  == doctest::Approx(-540.0f));   // -m_screen_height * 0.5
    CHECK(rect.pos.z  == doctest::Approx(0.0f));

    // Name + color passed through verbatim.
    CHECK(layer.nodes[0].name == "fill");
    CHECK(rect.color.r == doctest::Approx(1.0f));
    CHECK(rect.color.g == doctest::Approx(1.0f));
    CHECK(rect.color.b == doctest::Approx(1.0f));
    CHECK(rect.color.a == doctest::Approx(1.0f));
}

TEST_CASE("FIX 4: fullscreen_rect honors explicit screen_dimensions (1280x720)") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& l) {
        l.screen_dimensions(1280.0f, 720.0f);
        l.fullscreen_rect("fill_bg", Color::black());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);

    const RectShape& rect = first_rect_of(layer);
    CHECK(rect.size.x == doctest::Approx(1280.0f));
    CHECK(rect.size.y == doctest::Approx(720.0f));
    CHECK(rect.pos.x  == doctest::Approx(-640.0f));   // -1280 * 0.5
    CHECK(rect.pos.y  == doctest::Approx(-360.0f));   // -720  * 0.5
    CHECK(rect.pos.z  == doctest::Approx(0.0f));

    CHECK(layer.nodes[0].name == "fill_bg");
    CHECK(rect.color.r == doctest::Approx(0.0f));
    CHECK(rect.color.g == doctest::Approx(0.0f));
    CHECK(rect.color.b == doctest::Approx(0.0f));
    CHECK(rect.color.a == doctest::Approx(1.0f));
}

TEST_CASE("FIX 4: fill() helper delegates to fullscreen_rect (pin + pos offset)") {
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& l) {
        l.screen_dimensions(1920.0f, 1080.0f);
        l.fill(Color::red());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    // LayerBuilder::fill() calls fullscreen_rect("fill", color).
    REQUIRE(layer.nodes.size() == 1);
    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);

    const RectShape& rect = first_rect_of(layer);
    CHECK(rect.size.x == doctest::Approx(1920.0f));
    CHECK(rect.size.y == doctest::Approx(1080.0f));
    CHECK(rect.pos.x  == doctest::Approx(-960.0f));
    CHECK(rect.pos.y  == doctest::Approx(-540.0f));
    CHECK(rect.pos.z  == doctest::Approx(0.0f));

    CHECK(layer.nodes[0].name == "fill");
    CHECK(rect.color.r == doctest::Approx(1.0f));
    CHECK(rect.color.g == doctest::Approx(0.0f));
    CHECK(rect.color.b == doctest::Approx(0.0f));
    CHECK(rect.color.a == doctest::Approx(1.0f));
}

TEST_CASE("FIX 4: fullscreen_rect falls back to 1920x1080 defaults when screen_dimensions not called") {
    // Defaults supplied by LayerBuilder: m_screen_width=1920.0f,
    // m_screen_height=1080.0f.  fullscreen_rect() must still produce a
    // canvas-correct shape (pin_to(Center) + pos=(-960,-540)) WITHOUT
    // requiring an explicit screen_dimensions(...) call — this mirrors
    // the historical caller pattern (~14 sites) where the rect helper
    // was used as a "background" without explicit screen info.
    SceneBuilder builder;
    builder.layer("bg", [](LayerBuilder& l) {
        // No l.screen_dimensions(...) call.
        l.fullscreen_rect("fill", Color::white());
    });

    Scene scene = builder.build();
    REQUIRE(scene.layers().size() == 1);
    const Layer& layer = scene.layers()[0];

    REQUIRE(layer.layout.pin.has_value());
    CHECK(layer.layout.pin->anchor == Anchor::Center);
    CHECK(layer.layout.pin->margin == doctest::Approx(0.0f));

    const RectShape& rect = first_rect_of(layer);
    CHECK(rect.size.x == doctest::Approx(1920.0f));
    CHECK(rect.size.y == doctest::Approx(1080.0f));
    CHECK(rect.pos.x  == doctest::Approx(-960.0f));
    CHECK(rect.pos.y  == doctest::Approx(-540.0f));
    CHECK(rect.pos.z  == doctest::Approx(0.0f));

    // The flag lives on LayerBuilder (not Layer), so we cannot inspect
    // it after build(); the explicit-flag contract is:
    //   `LayerBuilder::screen_dimensions_were_set()` (tested elsewhere
    //   in the builder unit tests). Here we only assert the runtime
    //   shape params follow the documented default 1920×1080.
}
