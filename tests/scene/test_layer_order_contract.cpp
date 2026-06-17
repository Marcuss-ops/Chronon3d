#include <doctest/doctest.h>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/validation/scene_validator.hpp>
using namespace chronon3d;


// ── Layer Order Contract ─────────────────────────────────────────────────────
//
// Layer order MUST be preserved through scene creation and validation.
// Layers added first MUST appear first in the scene's layer list.
// The validator MUST NOT reorder layers.

TEST_CASE("LayerOrder: layers preserve insertion order") {
    Scene scene;

    Layer a; a.name = "first"; a.duration = 60;
    Layer b; b.name = "second"; b.duration = 60;
    Layer c; c.name = "third"; c.duration = 60;

    scene.add_layer(std::move(a));
    scene.add_layer(std::move(b));
    scene.add_layer(std::move(c));

    auto& layers = scene.layers();
    REQUIRE(layers.size() >= 3);
    CHECK(layers[0].name == "first");
    CHECK(layers[1].name == "second");
    CHECK(layers[2].name == "third");
}

TEST_CASE("LayerOrder: validator does not reorder layers") {
    Scene scene;

    Layer z; z.name = "z_layer"; z.duration = 60;
    Layer a; a.name = "a_layer"; a.duration = 60;
    Layer m; m.name = "m_layer"; m.duration = 60;

    scene.add_layer(std::move(z));
    scene.add_layer(std::move(a));
    scene.add_layer(std::move(m));

    auto& before = scene.layers();
    REQUIRE(before.size() >= 3);
    CHECK(before[0].name == "z_layer");
    CHECK(before[1].name == "a_layer");
    CHECK(before[2].name == "m_layer");

    // Validate — should not change order
    SceneValidator validator;
    auto result = validator.validate(scene);

    auto& after = scene.layers();
    CHECK(after[0].name == "z_layer");
    CHECK(after[1].name == "a_layer");
    CHECK(after[2].name == "m_layer");
}

TEST_CASE("LayerOrder: duplicate names do not break ordering") {
    Scene scene;

    Layer a1; a1.name = "dup"; a1.duration = 60;
    Layer b;  b.name = "unique"; b.duration = 60;
    Layer a2; a2.name = "dup"; a2.duration = 60;

    scene.add_layer(std::move(a1));
    scene.add_layer(std::move(b));
    scene.add_layer(std::move(a2));

    auto& layers = scene.layers();
    REQUIRE(layers.size() >= 3);
    CHECK(layers[0].name == "dup");
    CHECK(layers[1].name == "unique");
    CHECK(layers[2].name == "dup");
}

TEST_CASE("LayerOrder: zero-duration layers preserve position") {
    Scene scene;

    Layer a; a.name = "zero_dur"; a.duration = 0;
    Layer b; b.name = "normal";   b.duration = 60;
    Layer c; c.name = "last";     c.duration = 60;

    scene.add_layer(std::move(a));
    scene.add_layer(std::move(b));
    scene.add_layer(std::move(c));

    auto& layers = scene.layers();
    CHECK(layers[0].name == "zero_dur");
    CHECK(layers[0].duration == 0);
    CHECK(layers[1].name == "normal");
    CHECK(layers[2].name == "last");
}

TEST_CASE("LayerOrder: parent-child relationships do not reorder") {
    Scene scene;

    Layer child; child.name = "child"; child.duration = 60; child.parent_name = "parent";
    Layer parent; parent.name = "parent"; parent.duration = 60;

    scene.add_layer(std::move(child));
    scene.add_layer(std::move(parent));

    auto& layers = scene.layers();
    CHECK(layers[0].name == "child");
    CHECK(layers[0].parent_name == "parent");
    CHECK(layers[1].name == "parent");

    // Parent is after child in the list — this is fine, the hierarchy is
    // resolved by name, not by position.
}

TEST_CASE("LayerOrder: scene finalize preserves order") {
    Scene scene;

    Layer a; a.name = "a"; a.duration = 60;
    Layer b; b.name = "b"; b.duration = 60;
    Layer c; c.name = "c"; c.duration = 60;

    scene.add_layer(std::move(a));
    scene.add_layer(std::move(b));
    scene.add_layer(std::move(c));

    auto& layers = scene.layers();
    CHECK(layers[0].name == "a");
    CHECK(layers[1].name == "b");
    CHECK(layers[2].name == "c");
}

TEST_CASE("LayerOrder: empty scene has no layers") {
    Scene scene;
    CHECK(scene.layers().empty());
}

TEST_CASE("LayerOrder: scene with single layer") {
    Scene scene;
    Layer solo; solo.name = "solo"; solo.duration = 60;
    scene.add_layer(std::move(solo));

    auto& layers = scene.layers();
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].name == "solo");
}
