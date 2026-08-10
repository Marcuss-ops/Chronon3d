#include <doctest/doctest.h>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/clip_transition.hpp>

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>

using namespace chronon3d;

static Scene evaluate_frame(const Composition& composition, Frame frame,
                            bool with_runtime = false) {
    const runtime::RenderRuntime* render_runtime = nullptr;
    FontEngine* font_engine = nullptr;
    if (with_runtime) {
        static auto runtime = runtime::RenderRuntime::create(
            runtime::RuntimeConfig{Config{}, std::nullopt}).value();
        render_runtime = runtime.get();
        font_engine = &runtime->font_engine();
    }
    const auto sample = SampleTime::from_frame_int(frame, composition.frame_rate());
    return composition.evaluate(make_frame_context({
        .global_time = sample,
        .duration = composition.duration(),
        .width = composition.width(),
        .height = composition.height(),
        .font_engine = font_engine,
        .runtime = render_runtime,
    }));
}

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
        auto scene = evaluate_frame(comp, Frame{0}, true);
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
        auto scene = evaluate_frame(comp, Frame{0}, true);
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
        auto scene = evaluate_frame(comp, Frame{0}, true);
        CHECK(scene.layers().size() >= 0);
    }
}

#ifdef CHRONON3D_HAS_CONTENT_MINIMALIST
TEST_CASE("LightTransitionSoundSmoke: registers 60-frame LightLeak composition") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

    REQUIRE(registry.contains("LightTransitionSoundSmoke"));
    const auto comp = registry.create("LightTransitionSoundSmoke");
    CHECK(comp.width() == 1920);
    CHECK(comp.height() == 1080);
    CHECK(comp.frame_rate().numerator == 30);
    CHECK(comp.frame_rate().denominator == 1);
    CHECK(comp.duration() == Frame{60});

    const auto scene = evaluate_frame(comp, Frame{20});
    REQUIRE(scene.clip_transitions().size() == 1);
    const auto& transition = scene.clip_transitions().front();
    CHECK(transition.layer_a == "scene_a");
    CHECK(transition.layer_b == "scene_b");
    CHECK(transition.spec.kind == ClipTransitionKind::LightLeak);
    CHECK(transition.spec.flash_color == Color::white());
    CHECK(transition.from == Frame{20});
    CHECK(transition.duration == Frame{12});
}

TEST_CASE("LightTransition orange variants: register distinct LightLeak colors") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

    for (const auto* name : {"LightTransitionOrangeFlash", "LightTransitionAmberFlash",
                             "LightTransitionCopperFlash"}) {
        REQUIRE(registry.contains(name));
        const auto comp = registry.create(name);
        const auto scene = evaluate_frame(comp, Frame{26});
        REQUIRE(scene.clip_transitions().size() == 1);
        const auto& transition = scene.clip_transitions().front();
        CHECK(transition.spec.kind == ClipTransitionKind::LightLeak);
        CHECK(transition.spec.flash_color.r > transition.spec.flash_color.b);
        CHECK(transition.spec.flash_color.g > transition.spec.flash_color.b);
        CHECK(transition.from == Frame{20});
        CHECK(transition.duration == Frame{12});

        const auto& layers = scene.layers();
        CHECK(layers.size() >= 4);
        CHECK(std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return layer.name == "light_leak_flare";
        }));
    }
}

TEST_CASE("LightTransition: authored layer topology is stable across timeline boundaries") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

    const auto comp = registry.create("LightTransitionCopperFlash");
    const std::array<Frame, 7> boundary_frames = {
        Frame{0}, Frame{19}, Frame{20}, Frame{31}, Frame{32}, Frame{35}, Frame{59}
    };

    // Evaluate without a runtime font engine so this contract isolates the
    // authored layer/node topology. The runtime-dependent text additions are
    // not frame-dependent and are covered by the composition render tests.
    const auto baseline = evaluate_frame(comp, boundary_frames.front());
    REQUIRE(baseline.clip_transitions().size() == 1);
    const auto& baseline_layers = baseline.layers();
    REQUIRE(baseline_layers.size() == 6);
    const std::array<const char*, 6> expected_layer_names = {
        "scene_a", "scene_b", "light_leak_band_0", "light_leak_band_1",
        "light_leak_band_2", "light_leak_flare"
    };
    const std::array<const char*, 6> expected_node_names = {
        "scene_a_background", "scene_b_background", "streak", "streak", "streak", "flare"
    };

    for (const auto frame : boundary_frames) {
        const auto scene = evaluate_frame(comp, frame);
        REQUIRE(scene.clip_transitions().size() == baseline.clip_transitions().size());
        REQUIRE(scene.layers().size() == baseline_layers.size());

        for (std::size_t index = 0; index < baseline_layers.size(); ++index) {
            INFO("frame=" << frame.integral() << " layer_index=" << index);
            CHECK(scene.layers()[index].name == baseline_layers[index].name);
            CHECK(scene.layers()[index].name == expected_layer_names[index]);
            CHECK(scene.layers()[index].kind == baseline_layers[index].kind);
            CHECK(scene.layers()[index].from == Frame{0});
            CHECK(scene.layers()[index].duration == Frame{60});
            REQUIRE(scene.layers()[index].nodes.size() == baseline_layers[index].nodes.size());
            REQUIRE(scene.layers()[index].nodes.size() == 1);
            CHECK(scene.layers()[index].nodes.front().name == expected_node_names[index]);
        }
    }
}

TEST_CASE("LightTransition: opacity zero preserves authored node identity and shape validity") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);

    const auto comp = registry.create("LightTransitionCopperFlash");
    const std::array<Frame, 3> zero_opacity_frames = {
        Frame{0}, Frame{20}, Frame{32}
    };
    const std::array<const char*, 4> expected_zero_opacity_layers = {
        "light_leak_band_0", "light_leak_band_1", "light_leak_band_2",
        "light_leak_flare"
    };
    const auto baseline = evaluate_frame(comp, zero_opacity_frames.front());

    for (const auto frame : zero_opacity_frames) {
        const auto scene = evaluate_frame(comp, frame);
        REQUIRE(scene.layers().size() == 6);

        for (const auto* expected_name : expected_zero_opacity_layers) {
            const auto baseline_it = std::find_if(
                baseline.layers().begin(), baseline.layers().end(),
                [expected_name](const auto& layer) {
                    return layer.name == expected_name;
                });
            REQUIRE(baseline_it != baseline.layers().end());
            REQUIRE(baseline_it->nodes.size() == 1);

            const auto layer_it = std::find_if(
                scene.layers().begin(), scene.layers().end(),
                [expected_name](const auto& layer) {
                    return layer.name == expected_name;
                });
            REQUIRE(layer_it != scene.layers().end());
            CHECK(layer_it->kind == baseline_it->kind);
            CHECK(layer_it->nodes.size() == baseline_it->nodes.size());
            CHECK(layer_it->transform.opacity == doctest::Approx(0.0f));
            REQUIRE(layer_it->nodes.size() == 1);
            CHECK(layer_it->nodes.front().name == baseline_it->nodes.front().name);
            CHECK(layer_it->nodes.front().shape.type() ==
                  baseline_it->nodes.front().shape.type());
            CHECK(layer_it->nodes.front().shape.type() != ShapeType::None);
        }
    }
}

#endif

TEST_CASE("All registered compositions: evaluate at mid-duration frame") {
    CompositionRegistry registry;
    ensure_content_registered_smoke(registry);
    auto ids = registry.available();

    for (const auto& name : ids) {
        auto comp = registry.create(name);
        Frame mid = comp.duration() / 2;
        auto scene = evaluate_frame(comp, mid, true);
        CHECK(scene.layers().size() >= 0);
    }
}
