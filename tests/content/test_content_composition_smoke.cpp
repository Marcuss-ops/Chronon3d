#include <doctest/doctest.h>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <string>
#include <vector>

using namespace chronon3d;

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_TEXT) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
#endif


// ── Smoke tests: each composition creates and evaluates at frame 0 without crashing ──

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5: core 2.5D scenes evaluate frame 0") {
    register_content_modules();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "ParallaxSimple", "DepthScene", "CardFlip", "DofShowcase"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("2D5: camera test compositions evaluate frame 0") {
    register_content_modules();
    CompositionRegistry registry;

    const std::vector<std::string> names = {
        "CameraOrbitTargetLockTest", "CameraDollyPerspectiveScaleTest",
        "CameraParentNullRigTest", "CameraRollPanTiltGridTest"
    };

    for (const auto& name : names) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 1);
    }
}
#endif // CHRONON3D_BUILD_DIAGNOSTICS

#endif // CHRONON3D_HAS_CONTENT_2D5

// ── Cross-module: all available compositions evaluate safely ──────────────────

TEST_CASE("All registered compositions: every available composition evaluates frame 0") {
    CompositionRegistry registry;
    auto ids = registry.available();

    REQUIRE(ids.size() > 0);

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        CHECK(comp.name() == name);
        CHECK(comp.width() > 0);
        CHECK(comp.height() > 0);
        auto scene = comp.evaluate(Frame{0});
        CHECK(scene.layers().size() >= 0);
    }
}

TEST_CASE("All registered compositions: evaluate at mid-duration frame") {
    CompositionRegistry registry;
    auto ids = registry.available();

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        Frame mid = comp.duration() / 2;
        auto scene = comp.evaluate(mid);
        CHECK(scene.layers().size() >= 0);
    }
}
