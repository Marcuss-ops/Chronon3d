// ─── test_visual_preset_materializer.cpp — VISUAL-SSOT-02 ─────────────────
//
// Locks the IMAGE path of the single VisualPresetMaterializer: an image
// preset (image_focus_in, image_*_*, …) must resolve to a concrete anchor
// intent + animation intent through the same registry — it must NOT remain a
// purely descriptive registry entry.

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/visual_preset_materializer.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

using chronon3d::CanvasInfo;
using chronon3d::Frame;
using chronon3d::registry::builtin_visual_preset_registry;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::VisualPresetMaterializer;
using chronon3d::render_plan::measure_visual_bounds;

namespace {

// __FILE__ == <repo>/tests/registry/test_visual_preset_materializer.cpp
std::filesystem::path repo_root() {
    return std::filesystem::absolute(__FILE__).parent_path().parent_path().parent_path();
}

// Unique, empty mount root: the resolver resolves font paths UNDER it, but no
// font exists there, so FontEngine shaping fails and measure_visual_bounds
// must fall back to the preset box (this is deterministic regardless of the
// test process's current working directory).
struct EmptyMountDir {
    std::filesystem::path path;
    EmptyMountDir() {
        path = std::filesystem::temp_directory_path() /
               ("chronon_bounds_" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~EmptyMountDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

LayerPlan image_layer(std::string preset) {
    LayerPlan layer;
    layer.id = "img";
    layer.type = LayerType::Image;
    layer.asset = "portrait.png";
    layer.preset = std::move(preset);
    layer.box_width = 560.0f;
    layer.box_height = 560.0f;
    return layer;
}

} // namespace

TEST_CASE("VisualPresetMaterializer: image preset resolves anchor + animation") {
    const auto& registry = builtin_visual_preset_registry();
    const VisualPresetMaterializer materializer;
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    const auto resolved = materializer.materialize_image(
        image_layer("image_focus_in"), canvas, "discovery", registry, Frame{90});

    CHECK(resolved.preset_id == "image_focus_in");
    CHECK(resolved.layout.intent == "image_right");
    CHECK_FALSE(resolved.layout.fallback_intents.empty());
    CHECK(resolved.layout.width == doctest::Approx(560.0f));
    CHECK(resolved.layout.height == doctest::Approx(560.0f));
    // Registry animation defaults (motion + enter/exit) drive execution.
    CHECK(resolved.animation.preset == "focus_in");
    CHECK(resolved.animation.enter_duration == Frame{8});
    CHECK(resolved.animation.exit_duration == Frame{6});
}

TEST_CASE("VisualPresetMaterializer: image bounds default to the canvas") {
    const auto& registry = builtin_visual_preset_registry();
    const VisualPresetMaterializer materializer;
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    auto layer = image_layer("image_fade_in");
    layer.box_width.reset();
    layer.box_height.reset();

    const auto resolved = materializer.materialize_image(
        layer, canvas, "discovery", registry, Frame{90});

    CHECK(resolved.layout.width == doctest::Approx(1920.0f));
    CHECK(resolved.layout.height == doctest::Approx(1080.0f));
}

TEST_CASE("VisualPresetMaterializer: image materialization rejects text presets") {
    const auto& registry = builtin_visual_preset_registry();
    const VisualPresetMaterializer materializer;
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    const auto layer = image_layer("caption_card");
    CHECK_THROWS_AS(
        materializer.materialize_image(layer, canvas, "discovery", registry, Frame{90}),
        std::runtime_error);
}

TEST_CASE("VisualPresetMaterializer: measure_visual_bounds uses real font metrics + shaping") {
    const auto& registry = builtin_visual_preset_registry();
    const VisualPresetMaterializer materializer;
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    LayerPlan layer;
    layer.id = "person";
    layer.type = LayerType::Text;
    layer.text = "Tim Cook";
    layer.preset = "lower_third_safe";
    const auto resolved = materializer.materialize(
        layer, canvas, "discovery", registry, Frame{90});

    // The authored layout box is a canvas fraction (1640×100); the real
    // footprint must be shaped from the actual text, so far smaller.
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(repo_root());
    chronon3d::FontEngine engine(resolver);
    const auto bounds = measure_visual_bounds(resolved, engine);

    CHECK(bounds.width > 0.0f);
    CHECK(bounds.height > 0.0f);
    CHECK(bounds.width < resolved.layout.width);

    // Longer text → wider real bounds (shaping actually measures content,
    // not a fixed canvas-fraction box).
    auto long_layer = layer;
    long_layer.text = "This is a much longer lower third caption";
    const auto long_resolved = materializer.materialize(
        long_layer, canvas, "discovery", registry, Frame{90});
    const auto long_bounds = measure_visual_bounds(long_resolved, engine);
    CHECK(long_bounds.width > bounds.width);
}

TEST_CASE("VisualPresetMaterializer: measure_visual_bounds falls back to the preset box when the font is unavailable") {
    const auto& registry = builtin_visual_preset_registry();
    const VisualPresetMaterializer materializer;
    const CanvasInfo canvas = CanvasInfo::from_dimensions(1920.0f, 1080.0f);

    LayerPlan layer;
    layer.id = "person";
    layer.type = LayerType::Text;
    layer.text = "Tim Cook";
    layer.preset = "lower_third_safe";
    const auto resolved = materializer.materialize(
        layer, canvas, "discovery", registry, Frame{90});

    // Resolver mounted at a font-less root → the font cannot be loaded →
    // measure_visual_bounds keeps a sane non-zero footprint (the preset's
    // authored box) instead of emitting a zero-size overlay.
    EmptyMountDir empty;
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(empty.path);
    chronon3d::FontEngine engine(resolver);
    const auto bounds = measure_visual_bounds(resolved, engine);

    CHECK(bounds.width == doctest::Approx(resolved.layout.width));
    CHECK(bounds.height == doctest::Approx(resolved.layout.height));
}
