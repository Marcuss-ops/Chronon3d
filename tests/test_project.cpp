#include <doctest/doctest.h>
#include <chronon3d/internal/project.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

static Scene make_simple_scene(const FrameContext& ctx) {
    SceneBuilder s(ctx);
    s.layer("bg", [&ctx](LayerBuilder& l) {
        l.rect("bg_rect", RectParams{
            .size = {static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)},
            .color = Color{0.1f, 0.1f, 0.15f, 1.0f},
            .pos = {static_cast<f32>(ctx.width) * 0.5f,
                    static_cast<f32>(ctx.height) * 0.5f,
                    0.0f},
        });
    });
    return s.build();
}

// ── Tests ────────────────────────────────────────────────────────────────

TEST_CASE("Project: default metadata") {
    Project project;
    CHECK(project.name == "Untitled Project");
    CHECK(project.default_width == 1920);
    CHECK(project.default_height == 1080);
    CHECK(project.default_fps == 30);
    CHECK(project.assets_root == "");
    CHECK(project.size() == 0);
}

TEST_CASE("Project: register and create composition with lambda") {
    Project project;
    project.name = "TestProject";
    project.default_width = 640;
    project.default_height = 360;

    project.composition("TitleCard", {.duration = Frame{150}}, make_simple_scene);

    CHECK(project.size() == 1);
    CHECK(project.contains("TitleCard"));
    CHECK(!project.contains("Nonexistent"));

    auto comp = project.create("TitleCard");
    CHECK(comp.name() == "TitleCard");
    CHECK(comp.width() == 640);
    CHECK(comp.height() == 360);
    CHECK(comp.duration() == Frame{150});
}

TEST_CASE("Project: per-composition overrides override project defaults") {
    Project project;
    project.default_width = 1920;
    project.default_height = 1080;
    project.default_fps = 30;

    project.composition("WideShowcase",
        {.width = 2560, .height = 1440, .fps = 60, .duration = Frame{300}},
        make_simple_scene);

    auto comp = project.create("WideShowcase");
    CHECK(comp.width() == 2560);
    CHECK(comp.height() == 1440);
    CHECK(comp.duration() == Frame{300});
}

TEST_CASE("Project: multiple compositions registered alphabetically") {
    Project project;

    project.composition("Zebra", {.duration = Frame{10}}, make_simple_scene);
    project.composition("Alpha", {.duration = Frame{20}}, make_simple_scene);
    project.composition("Middle", {.duration = Frame{30}}, make_simple_scene);

    CHECK(project.size() == 3);

    auto names = project.available();
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Alpha");
    CHECK(names[1] == "Middle");
    CHECK(names[2] == "Zebra");
}

TEST_CASE("Project: duplicate composition throws") {
    Project project;
    project.composition("Dup", {.duration = Frame{10}}, make_simple_scene);
    CHECK_THROWS_AS(
        project.composition("Dup", {.duration = Frame{10}}, make_simple_scene),
        std::runtime_error);
}

TEST_CASE("Project: registry accessor provides direct CompositionRegistry access") {
    Project project;
    project.composition("A", {.duration = Frame{10}}, make_simple_scene);

    auto& reg = project.registry();
    CHECK(reg.contains("A"));
    CHECK(reg.available().size() == 1);

    // Direct registry add also visible through project
    reg.add("B", [](const CompositionProps&) -> Composition {
        return composition(
            CompositionSpec{.name = "B", .width = 100, .height = 100},
            [](const FrameContext&) -> Scene { return Scene{}; });
    });
    CHECK(project.size() == 2);
    CHECK(project.contains("B"));
}

TEST_CASE("Project: composition evaluates at frame 0") {
    Project project;
    project.default_width = 320;
    project.default_height = 180;

    project.composition("Eval", {.duration = Frame{60}}, make_simple_scene);

    auto comp = project.create("Eval");
    auto scene = comp.evaluate(Frame{0});
    CHECK(scene.layers().size() >= 1);
}

TEST_CASE("Project: assets_root threaded to composition") {
    Project project;
    project.assets_root = "/tmp/test_assets";

    project.composition("WithAssets", {.duration = Frame{1}}, make_simple_scene);

    auto comp = project.create("WithAssets");
    CHECK(comp.assets_root() == "/tmp/test_assets");
}

TEST_CASE("Project: add() for direct factory registration") {
    Project project;

    project.add("FromFactory",
        [](const CompositionProps& /*props*/) -> Composition {
            return composition(
                CompositionSpec{.name = "FromFactory", .width = 800, .height = 600},
                [](const FrameContext&) -> Scene { return Scene{}; });
        });

    CHECK(project.contains("FromFactory"));
    auto comp = project.create("FromFactory");
    CHECK(comp.name() == "FromFactory");
    CHECK(comp.width() == 800);
    CHECK(comp.height() == 600);
}
