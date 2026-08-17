// ─── test_visual_preset_materializer.cpp — VISUAL-SSOT-02 ─────────────────
//
// Locks the IMAGE path of the single VisualPresetMaterializer: an image
// preset (image_focus_in, image_*_*, …) must resolve to a concrete anchor
// intent + animation intent through the same registry — it must NOT remain a
// purely descriptive registry entry.

#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/visual_preset_materializer.hpp>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <utility>

using chronon3d::CanvasInfo;
using chronon3d::Frame;
using chronon3d::registry::builtin_visual_preset_registry;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::VisualPresetMaterializer;

namespace {

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
