#include <doctest/doctest.h>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/registry/font_registry.hpp>
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <chronon3d/animation/spring.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

using namespace chronon3d;

TEST_CASE("AssetRegistry mounts and resolves paths correctly") {
    AssetRegistry::clear();
    AssetRegistry::mount("C:/Users/pater/Pyt/Chrono/assets");

    std::string path = AssetRegistry::resolve("images/logo.png");
    CHECK(std::filesystem::path(path) == std::filesystem::path("C:/Users/pater/Pyt/Chrono/assets/images/logo.png"));

    // Dynamic asset deductions
    std::string image_deduced = asset("images/logo.png");
    CHECK(std::filesystem::path(image_deduced) == std::filesystem::path("C:/Users/pater/Pyt/Chrono/assets/images/logo.png"));

    auto assets = AssetRegistry::instance().assets();
    CHECK(!assets.empty());
    CHECK(assets[0].path.filename().string() == "logo.png");
}

TEST_CASE("FontRegistry resolves exact or closest weights/styles") {
    FontRegistry::clear();
    FontRegistry::register_font({
        .family = "Inter",
        .weight = 700,
        .style = "normal",
        .path = "fonts/Inter-Bold.ttf"
    });

    std::string path = FontRegistry::resolve("Inter", 700, "normal");
    CHECK(path == "fonts/Inter-Bold.ttf");

    // Fallback logic check
    std::string closest = FontRegistry::resolve("Inter", 800, "italic");
    CHECK(closest == "fonts/Inter-Bold.ttf");
}


TEST_CASE("Interpolation and Spring physical presets function correctly") {
    // Easing interpolation
    f32 interpolated = interpolate(15.0f, 0.0f, 30.0f, 100.0f, 200.0f);
    CHECK(interpolated == doctest::Approx(150.0f));

    // Extrapolate vs Clamp
    f32 clamped = interpolate(45.0f, 0.0f, 30.0f, 100.0f, 200.0f, Easing::Linear, ClampMode::Clamp);
    f32 extrapolated = interpolate(45.0f, 0.0f, 30.0f, 100.0f, 200.0f, Easing::Linear, ClampMode::Extrapolate);

    CHECK(clamped == doctest::Approx(200.0f));
    CHECK(extrapolated == doctest::Approx(250.0f));

    // Fluent anim builder
    f32 animated = anim(15.0f).map(0.0f, 30.0f, 10.0f, 20.0f);
    CHECK(animated == doctest::Approx(15.0f));

    // Spring presets evaluation
    f32 gentle_spring = spring(0.5f, 0.0f, 1.0f, Spring::Gentle);
    f32 snappy_spring = spring(0.5f, 0.0f, 1.0f, Spring::Snappy);
    CHECK(gentle_spring != snappy_spring);
}

TEST_CASE("SceneBuilder sequence temporal timeline shifting works correctly") {
    FrameContext ctx{
        .frame = Frame{45},
        .duration = Frame{100},
        .width = 1920,
        .height = 1080
    };

    SceneBuilder s(ctx);
    s.sequence("intro", {.from = 30, .duration = 60}, [](SceneBuilder& sub) {
        sub.layer("test_layer", [](LayerBuilder& l) {
            l.from(10).duration(20);
        });
    });

    Scene scene = s.build();
    auto& layers = scene.layers();
    REQUIRE(layers.size() == 1);
    
    // Shifted from 10 relative to 30 absolute -> 40 absolute
    CHECK(layers[0].from == 40);
    CHECK(layers[0].duration == 20);
}
