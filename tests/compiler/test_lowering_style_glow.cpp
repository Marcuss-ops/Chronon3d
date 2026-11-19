// ═══════════════════════════════════════════════════════════════════════════
// tests/compiler/test_lowering_style_glow.cpp
//
// Regression lock — `style.glow` on a non-text layer.
//
// The render-plan decoder accepts `style.glow` on any layer style (GlowStyle),
// but for a long time only the text materializer had a consumer for it: a
// shape / image / video / color card carrying `style.glow` lowered to a node
// with no glow entry at all, so the authored halo silently disappeared from
// the render while the plan validated clean.
//
// Fix under test: scene lowering appends the canonical Glow effect entry to
// the layer's own effect stack for those layer types, exactly like an authored
// `effects: [{kind: glow}]` entry, and leaves text layers alone (their halo is
// applied at materialization, before their animator entries).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "../../src/compiler/compiler_stages.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <string>

using namespace chronon3d;

namespace {

render_plan::RenderPlan canvas_with(render_plan::LayerType type) {
    render_plan::RenderPlan plan;
    plan.canvas.width = 640;
    plan.canvas.height = 360;
    plan.canvas.fps = FrameRate{24, 1};
    plan.canvas.duration = Frame{24};

    render_plan::LayerPlan layer;
    layer.id = "card";
    layer.type = type;
    layer.size = {320.0f, 180.0f};
    layer.size_dimensions = 2;
    layer.position = {0.0f, 0.0f, 0.0f};
    layer.position_dimensions = 2;

    render_plan::LayerStylePlan style;
    style.glow = render_plan::GlowStyle{
        .radius = 18.0f, .intensity = 0.6f, .color = "#66CCFF"};
    layer.style = style;

    plan.layers.push_back(std::move(layer));
    return plan;
}

std::size_t count_kind(const compiler::CompiledProgramData& data,
                       render_plan::LayerPlan::EffectKindPlan kind) {
    REQUIRE(data.layers.size() == 1);
    std::size_t found = 0;
    for (const auto& effect : data.layers.front().effects)
        if (effect.kind == kind) ++found;
    return found;
}

} // namespace

TEST_CASE("lowering: style.glow on a shape layer becomes a Glow effect") {
    auto plan = canvas_with(render_plan::LayerType::Shape);
    compiler::CompiledProgramData data;
    compiler::stages::lower_scene(plan, data);

    REQUIRE(count_kind(data, render_plan::LayerPlan::EffectKindPlan::Glow) == 1);
    const auto& glow = data.layers.front().effects.back();
    CHECK(glow.radius == doctest::Approx(18.0f));
    CHECK(glow.glow_intensity == doctest::Approx(0.6f));
    // Lowering must reproduce the decoder's colour mapping exactly (the
    // canonical parser resolves sRGB to linear working space).
    const auto expected = render_plan::parse_hex_color("#66CCFF");
    REQUIRE(expected.has_value());
    CHECK(glow.effect_color[0] == doctest::Approx(expected->r));
    CHECK(glow.effect_color[1] == doctest::Approx(expected->g));
    CHECK(glow.effect_color[2] == doctest::Approx(expected->b));
    CHECK(glow.effect_color[3] == doctest::Approx(expected->a));
    // Lowered after the authored effects: author order is preserved.
    CHECK(glow.canonical_effect_id == std::string("glow"));
}

TEST_CASE("lowering: style.glow is lowered for image, video and color layers") {
    for (const auto type : {render_plan::LayerType::Image,
                            render_plan::LayerType::Video,
                            render_plan::LayerType::Color}) {
        auto plan = canvas_with(type);
        compiler::CompiledProgramData data;
        compiler::stages::lower_scene(plan, data);
        CHECK(count_kind(data, render_plan::LayerPlan::EffectKindPlan::Glow) == 1);
    }
}

TEST_CASE("lowering: text layers keep their materializer glow (none added here)") {
    auto plan = canvas_with(render_plan::LayerType::Text);
    compiler::CompiledProgramData data;
    compiler::stages::lower_scene(plan, data);

    CHECK(count_kind(data, render_plan::LayerPlan::EffectKindPlan::Glow) == 0);
}

TEST_CASE("lowering: 3D text keeps the authored absolute position") {
    // The v3 typed lane used to rebase every non-extruded text layer by half a
    // canvas. Screen-space text gets that centre added back during placement,
    // but a camera-projected 3D caption never does, so the rebase pinned it to
    // the left edge (x 960 -> 0 on a 1920 canvas).
    auto plan = canvas_with(render_plan::LayerType::Text);
    plan.layers.front().position = {320.0f, 180.0f, 0.0f};
    plan.layers.front().enable_3d = true;
    compiler::CompiledProgramData data;
    compiler::stages::lower_scene(plan, data);

    REQUIRE(data.layers.size() == 1);
    CHECK(data.layers.front().position[0] == doctest::Approx(320.0f));
    CHECK(data.layers.front().position[1] == doctest::Approx(180.0f));

    // Screen-space text keeps the centre-relative rebase: placement adds the
    // canvas centre back exactly once.
    auto flat = canvas_with(render_plan::LayerType::Text);
    flat.layers.front().position = {320.0f, 180.0f, 0.0f};
    compiler::CompiledProgramData flat_data;
    compiler::stages::lower_scene(flat, flat_data);
    REQUIRE(flat_data.layers.size() == 1);
    CHECK(flat_data.layers.front().position[0] == doctest::Approx(0.0f));
    CHECK(flat_data.layers.front().position[1] == doctest::Approx(0.0f));
}

TEST_CASE("lowering: a layer without style.glow adds no glow effect") {
    auto plan = canvas_with(render_plan::LayerType::Shape);
    plan.layers.front().style.reset();
    compiler::CompiledProgramData data;
    compiler::stages::lower_scene(plan, data);

    CHECK(count_kind(data, render_plan::LayerPlan::EffectKindPlan::Glow) == 0);
}
