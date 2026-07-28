// ============================================================================
// test_render_plan_animation_timing.cpp — propagation test for
// `animation.start_frame` and `animation.duration_frames` from
// chronon-render-plan.v1 JSON to canonical RenderJob.
//
// Test scope (per chore TICKET-WIRE-ANIMATION-TIMING-JSON):
//   1. SUBCASE "nested animation block" — the user spec literal:
//      `plan {animation:{start_frame:30, duration_frames:120}}`
//      → `job.start_frame == 30 && job.duration_frames == 120`.
//   2. SUBCASE "no animation block" — layer top-level fallback.
//   3. SUBCASE "animation block with preset only" — falls back to layer
//      top-level (because animation block has no timing fields).
// ============================================================================

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include "utils/job/render_job.hpp"
#include "utils/job/render_plan_timing.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <memory>

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

CompositionRegistry make_test_registry() {
    CompositionRegistry registry;
    registry.add(TypedCompositionDescriptor<int>{
        .id = "test-comp",
        .category = "test",
        .defaults = 0,
        .resolve_metadata = [](int) {
            return CompositionMetadata{1920, 1080, FrameRate{30, 1}, Frame{300}};
        },
        .factory = [](int) {
            return composition({.name = "test-comp",
                                .width = 1920,
                                .height = 1080,
                                .duration = Frame{300}},
                               [](const FrameContext&) {});
        }
    }.to_descriptor());
    return registry;
}

// Minimal valid chronon-render-plan.v1 shape: schema + version + canvas + layers + output.
// Per the JSON schema, `additionalProperties: false` enforces strict shape at validator time.
// Here, for unit-test consumption, we use ONLY the keys we care about (no validator wired).
nlohmann::json base_plan_with_layer(const nlohmann::json& layer) {
    return nlohmann::json{
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"canvas", {{"width", 1920},
                     {"height", 1080},
                     {"fps", 30},
                     {"duration_frames", 300}}},
        {"layers", nlohmann::json::array({layer})},
        {"output", {{"path", "/tmp/test_animation_timing.mp4"}}},
    };
}

} // namespace

TEST_CASE("render_plan_animation_timing — nested animation block wins") {
    // User spec literal:
    //   plan {animation:{start_frame:30, duration_frames:120}}
    //     → job.start_frame == 30 && job.duration_frames == 120
    const nlohmann::json plan = base_plan_with_layer({
        {"id", "L1"},
        {"type", "color"},
        {"animation", {{"preset", "fade_in"},
                       {"start_frame", 30},
                       {"duration_frames", 120}}},
    });

    const AnimationTiming timing = extract_animation_timing(plan);
    CHECK(timing.start_frame == Frame{30});
    CHECK(timing.duration_frames == Frame{120});

    // Pipeline through the canonical RenderRequest → RenderJob.
    auto registry = make_test_registry();
    RenderRequest request;
    request.comp_id = "test-comp";
    request.mode = RenderMode::Sequence;
    request.start_frame = timing.start_frame;
    request.duration_frames = timing.duration_frames;
    request.output = "output/test.mp4";

    auto result = resolve_render_request(registry, std::move(request));
    REQUIRE(result.has_value());
    CHECK(result->start_frame == Frame{30});
    CHECK(result->duration_frames == Frame{120});
}

TEST_CASE("render_plan_animation_timing — no animation block (top-level fallback)") {
    // No `animation` key → fall back to layer top-level
    // `start_frame` / `duration_frames`.
    const nlohmann::json plan = base_plan_with_layer({
        {"id", "L1"},
        {"type", "color"},
        {"start_frame", 60},
        {"duration_frames", 90},
    });

    const AnimationTiming timing = extract_animation_timing(plan);
    CHECK(timing.start_frame == Frame{60});
    CHECK(timing.duration_frames == Frame{90});

    auto registry = make_test_registry();
    RenderRequest request;
    request.comp_id = "test-comp";
    request.mode = RenderMode::Sequence;
    request.start_frame = timing.start_frame;
    request.duration_frames = timing.duration_frames;
    request.output = "output/test.mp4";

    auto result = resolve_render_request(registry, std::move(request));
    REQUIRE(result.has_value());
    CHECK(result->start_frame == Frame{60});
    CHECK(result->duration_frames == Frame{90});
}

TEST_CASE("render_plan_animation_timing — animation.preset only (no timing fields, falls back to top-level)") {
    // `animation.preset` without timing fields — falls back to layer top-level.
    const nlohmann::json plan = base_plan_with_layer({
        {"id", "L1"},
        {"type", "color"},
        {"start_frame", 10},
        {"duration_frames", 25},
        {"animation", {{"preset", "fade_in"}}},
    });

    const AnimationTiming timing = extract_animation_timing(plan);
    CHECK(timing.start_frame == Frame{10});
    CHECK(timing.duration_frames == Frame{25});

    auto registry = make_test_registry();
    RenderRequest request;
    request.comp_id = "test-comp";
    request.mode = RenderMode::Sequence;
    request.start_frame = timing.start_frame;
    request.duration_frames = timing.duration_frames;
    request.output = "output/test.mp4";

    auto result = resolve_render_request(registry, std::move(request));
    REQUIRE(result.has_value());
    CHECK(result->start_frame == Frame{10});
    CHECK(result->duration_frames == Frame{25});
}
