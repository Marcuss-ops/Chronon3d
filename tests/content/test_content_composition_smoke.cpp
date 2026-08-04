#include <doctest/doctest.h>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/clip_transition.hpp>

#include <string>
#include <vector>

using namespace chronon3d;

#include <filesystem>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#endif

// ── Helper: register content into the given registry ─────────────────────────
#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
static void ensure_content_registered_smoke(CompositionRegistry& registry) {
    ExtensionCatalog cat;
    graph::GraphNodeCatalog nodes;
    effects::EffectCatalog effects;
    AssetRegistry assets;
    ExtensionContext ctx{registry, nodes, effects, assets};
    register_content_modules(cat, ctx);
}
#else
// Stub when content modules are not compiled.
// The cross-module smoke tests need at least built-in compositions.
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
static void ensure_content_registered(CompositionRegistry& registry) {
    chronon3d::register_builtin_compositions(registry);
}
#endif


// ── Smoke tests: each composition creates and evaluates at frame 0 without crashing ──

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5: core 2.5D scenes evaluate frame 0") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

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
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

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
    ensure_content_registered_smoke(registry);
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

#ifdef CHRONON3D_HAS_CONTENT_MINIMALIST
TEST_CASE("LightTransitionSoundSmoke: registers 60-frame Flash composition") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

    REQUIRE(registry.contains("LightTransitionSoundSmoke"));
    const auto comp = registry.create("LightTransitionSoundSmoke");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.frame_rate().numerator == 30);
    CHECK(comp.frame_rate().denominator == 1);
    CHECK(comp.duration() == Frame{60});

    const auto scene = comp.evaluate(Frame{20});
    REQUIRE(scene.clip_transitions().size() == 1);
    const auto& transition = scene.clip_transitions().front();
    CHECK(transition.layer_a == "scene_a");
    CHECK(transition.layer_b == "scene_b");
    CHECK(transition.spec.kind == ClipTransitionKind::Flash);
    CHECK(transition.spec.flash_color == Color::white());
    CHECK(transition.from == Frame{20});
    CHECK(transition.duration == Frame{12});
}

#endif

TEST_CASE("All registered compositions: evaluate at mid-duration frame") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);
    auto ids = registry.available();

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        Frame mid = comp.duration() / 2;
        auto scene = comp.evaluate(mid);
        CHECK(scene.layers().size() >= 0);
    }
}
