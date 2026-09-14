// =============================================================================
// tests/runtime/test_compiled_program_execution_equivalence.cpp
//
// Typed-program execution gate. The old render-plan comparison was removed
// with the legacy sampler bridge; this corpus checks deterministic output from
// the canonical CompiledProgram -> ProgramSampler -> graph path.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <array>
#include <string>
#include <vector>

namespace {

using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::RenderPlan;

RenderPlan make_plan(std::string job_id, int width, int height,
                     chronon3d::Frame duration,
                     std::vector<LayerPlan> layers) {
    RenderPlan plan;
    plan.job_id = std::move(job_id);
    plan.canvas.width = width;
    plan.canvas.height = height;
    plan.canvas.fps = {30, 1};
    plan.canvas.duration = duration;
    plan.layers = std::move(layers);
    return plan;
}

LayerPlan full_canvas_color(const std::string& id,
                            float r, float g, float b, float a) {
    LayerPlan layer;
    layer.id = id;
    layer.type = LayerType::Color;
    layer.color = {r, g, b, a};
    return layer;
}

/// Bit-exact pixel comparison over the logical framebuffer area (padding and
/// stride must never influence the gate).
bool bit_equal(const chronon3d::Framebuffer& a,
               const chronon3d::Framebuffer& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const auto pa = a.get_pixel(x, y);
            const auto pb = b.get_pixel(x, y);
            if (pa.r != pb.r || pa.g != pb.g || pa.b != pb.b || pa.a != pb.a) {
                return false;
            }
        }
    }
    return true;
}

/// Corpus entry: a render plan plus the frames the gate renders.
struct CorpusEntry {
    std::string name;
    RenderPlan plan;
    std::vector<chronon3d::Frame> frames;
};

std::vector<CorpusEntry> corpus() {
    std::vector<CorpusEntry> entries;

    // Solid full-canvas color.
    entries.push_back(CorpusEntry{
        "solid-red",
        make_plan("gate-solid-red", 48, 32, chronon3d::Frame{2},
                  {full_canvas_color("bg", 0.90f, 0.10f, 0.10f, 1.0f)}),
        {chronon3d::Frame{0}, chronon3d::Frame{1}}});

    // Overlapping layers exercise the composite stage (premultiplied blend).
    entries.push_back(CorpusEntry{
        "composite-overlap",
        make_plan("gate-composite-overlap", 48, 32, chronon3d::Frame{2},
                  {full_canvas_color("bg", 1.0f, 1.0f, 1.0f, 1.0f),
                   full_canvas_color("fg", 0.10f, 0.30f, 0.95f, 0.5f)}),
        {chronon3d::Frame{0}, chronon3d::Frame{1}}});

    // Longer static run: first and last frame of a 3-frame plan.
    entries.push_back(CorpusEntry{
        "solid-green-span",
        make_plan("gate-solid-green-span", 48, 32, chronon3d::Frame{3},
                  {full_canvas_color("bg", 0.10f, 0.90f, 0.10f, 1.0f)}),
        {chronon3d::Frame{0}, chronon3d::Frame{2}}});

    return entries;
}

}  // namespace

TEST_CASE("typed-program execution gate is deterministic (still frames)") {
    for (const auto& entry : corpus()) {
        SUBCASE(entry.name.c_str()) {
            chronon3d::assets::AssetResolver resolver;
            auto compiled = chronon3d::compiler::compile(entry.plan, resolver);
            REQUIRE(compiled);

            auto program = std::make_shared<const chronon3d::compiler::CompiledProgram>(
                std::move(compiled).value());

            chronon3d::RenderEngine engine;
            for (const auto frame : entry.frames) {
                CAPTURE(frame.integral());

                const auto first = engine.render_program(*program, frame);
                const auto second = engine.render_program(*program, frame);
                REQUIRE(first);
                REQUIRE(second);
                CHECK(first->width() == entry.plan.canvas.width);
                CHECK(first->height() == entry.plan.canvas.height);
                CHECK(bit_equal(*first, *second));
            }
        }
    }
}

TEST_CASE("typed-program overlay lowering preserves full-canvas color and image motion aliases") {
    auto plan = make_plan("overlay-lowering-regression", 48, 32,
                          chronon3d::Frame{34},
                          {full_canvas_color("bg", 0.9333333f, 0.945098f,
                                             0.9058823f, 1.0f)});
    plan.layers.front().start_frame = chronon3d::Frame{10};
    plan.layers.front().animation = chronon3d::render_plan::AnimationPlan{};
    plan.layers.front().animation->tracks.push_back({
        "position_x",
        {{chronon3d::Frame{0}, {-120.0f}},
         {chronon3d::Frame{12}, {0.0f}}},
        "linear"});
    plan.layers.front().animation->tracks.push_back({
        "scale",
        {{chronon3d::Frame{0}, {0.85f}},
         {chronon3d::Frame{12}, {1.0f}}},
        "linear"});

    chronon3d::assets::AssetResolver resolver;
    auto compiled = chronon3d::compiler::compile(plan, resolver);
    REQUIRE(compiled);
    auto program = std::move(compiled).value();

    const auto frame0 = chronon3d::compiler::TypedProgramSampler{}.sample(
        program, chronon3d::Frame{0});
    REQUIRE(frame0.layers.size() == 1);
    CHECK(frame0.layers.front().matrix[3][0] == doctest::Approx(-120.0f));
    CHECK(frame0.layers.front().matrix[0][0] == doctest::Approx(0.85f));

    const auto frame12 = chronon3d::compiler::TypedProgramSampler{}.sample(
        program, chronon3d::Frame{22});
    CHECK(frame12.layers.front().matrix[3][0] == doctest::Approx(0.0f));
    CHECK(frame12.layers.front().matrix[0][0] == doctest::Approx(1.0f));

    chronon3d::RenderEngine engine;
    const auto rendered = engine.render_program(program, chronon3d::Frame{22});
    REQUIRE(rendered);
    const auto pixel = rendered->get_pixel(0, 0);
    CHECK(pixel.r == doctest::Approx(0.9333333f).epsilon(0.02f));
    CHECK(pixel.g == doctest::Approx(0.945098f).epsilon(0.02f));
    CHECK(pixel.b == doctest::Approx(0.9058823f).epsilon(0.02f));
}
