// =============================================================================
// tests/runtime/test_compiled_program_execution_equivalence.cpp
//
// Compiler-equivalence gate (milestone 1) — TICKET-COMPILER-BOUNDARY-WAVE7-9.
//
// Two execution seams must produce identical still frames for the same plan:
//   * legacy seam  — render_compiled() executes the immutable
//                    CompiledComposition that PreparedRenderJob materialises
//                    (the compatibility payload the runtime currently uses);
//   * program seam — render_program() executes the compiler-boundary
//                    CompiledProgram through the canonical ProgramSampler +
//                    graph pipeline.
//
// Today both seams converge on the same legacy evaluation, so the gate pins
// the bit-exact baseline that must stay green while the compiled-program
// execution path (nodes/passes/parameters) is built and switched over.  Any
// future divergence between the two seams is a GATE FAIL by construction.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/compiler/compiled_program.hpp>
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

TEST_CASE("equivalence gate: compiled-program seam matches legacy "
          "compiled-composition seam (still frames)") {
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

                // Legacy seam — PreparedRenderJob materialises this payload.
                const auto legacy_frame = engine.render_compiled(
                    program->compiled_composition(), frame);
                REQUIRE(legacy_frame);

                // Program seam — canonical ProgramSampler + graph pipeline.
                const auto program_frame = engine.render_program(*program, frame);
                REQUIRE(program_frame);

                CHECK(legacy_frame->width() == program_frame->width());
                CHECK(legacy_frame->height() == program_frame->height());
                CHECK(bit_equal(*legacy_frame, *program_frame));
            }
        }
    }
}
