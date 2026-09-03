// =============================================================================
// test_sequential_graph_cache.cpp
//
// Sequential graph-cache verifier: for frames 0-59, the framebuffer produced
// by ONE shared runtime rendering the whole sequence must be byte-identical
// (XXH64 via test::framebuffer_hash) to the framebuffer produced by a FRESH
// runtime rendering that single frame.  Exercised in four orders:
//
//   * linear   0,1,2,...,59
//   * random   fixed-seed shuffle (deterministic)
//   * reverse  59,58,...,0
//   * repeated each frame twice (0,0,1,1,...)
//
// The shared runtime is the cache-under-test: every call observes the prior
// frame's compiled-graph cache, node cache, framebuffer pool and session
// history.  A fresh runtime per frame is the cold-cache reference.
//
// Fixture contract (canonical repo pattern):
//   * Temporal effects (motion blur) and dirty-rect/tile reuse are DISABLED so
//     the comparison isolates graph/node-cache output, not framebuffer
//     accumulation across frames.
//   * Diagnostics are intentionally disabled. A logging flag must not change
//     rendered pixels; keeping it OFF reproduces the production configuration
//     and guards against regressions that only appear when logging is disabled.
//
// The verifier runs with diagnostics OFF and remains green after the
// cache/static-classification fix. A failure reports the first divergent
// position and frame instead of hiding the mismatch behind logging.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

constexpr int kFirstFrame = 0;
constexpr int kLastFrame = 59;
constexpr int kWidth = 160;
constexpr int kHeight = 90;

Composition make_sequential_cache_composition() {
    return composition(
        {.name = "SequentialGraphCacheVerifier",
         .width = kWidth,
         .height = kHeight,
         .duration = kLastFrame + 1},
        [](const FrameContext& ctx) {
            SceneBuilder scene(ctx);
            scene.rect("background", {
                .size = {static_cast<float>(ctx.width), static_cast<float>(ctx.height)},
                .color = Color{0.035f, 0.055f, 0.10f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
            });

            // The moving source changes only dynamic values. Its authored
            // graph topology remains constant for every frame, which makes
            // this a focused cache-refresh regression fixture.
            const float frame = static_cast<float>(ctx.frame());
            const float x = -55.0f + frame * 1.8f;
            const float y = 8.0f * std::sin(frame * 0.11f);
            scene.rect("moving_source", {
                .size = {28.0f, 28.0f},
                .color = Color{0.95f, 0.35f, 0.12f, 1.0f},
                .pos = {x, y, 0.0f},
            });

            // A permanently authored node with exact zero opacity keeps the
            // zero-visibility contract in the same stable topology. It must
            // not be replaced by an epsilon workaround or removed per frame.
            scene.layer("zero_opacity", [&](LayerBuilder& layer) {
                layer.opacity(0.0f);
                layer.rect("zero_opacity_shape", {
                    .size = {12.0f, 12.0f},
                    .color = Color::white(),
                    .pos = {80.0f, 40.0f, 0.0f},
                });
            });

            return scene.build();
        });
}

std::vector<int> linear_order() {
    std::vector<int> order;
    order.reserve(kLastFrame - kFirstFrame + 1);
    for (int frame = kFirstFrame; frame <= kLastFrame; ++frame) {
        order.push_back(frame);
    }
    return order;
}

std::vector<int> random_order() {
    auto order = linear_order();
    std::mt19937 generator(0x5E0C3E1u);
    std::shuffle(order.begin(), order.end(), generator);
    return order;
}

std::vector<int> reverse_order() {
    auto order = linear_order();
    std::reverse(order.begin(), order.end());
    return order;
}

std::vector<int> repeated_order() {
    auto order = linear_order();
    std::vector<int> repeated;
    repeated.reserve(order.size() * 2);
    for (int frame : order) {
        repeated.push_back(frame);
        repeated.push_back(frame);
    }
    return repeated;
}

bool has_non_black_frame(const Framebuffer& framebuffer) {
    const float mean_luma = average_luma_rect(
        framebuffer, 0, 0, framebuffer.width(), framebuffer.height());
    // This fixture is intentionally dark; the authored background baseline is
    // approximately 0.0011, while a fully black framebuffer is exactly zero.
    return mean_luma > 0.0005f;
}

void verify_order(const std::string& order_name,
                  const std::vector<int>& order,
                  const Composition& composition_to_render) {
    // Temporal and framebuffer-reuse state must not participate in the
    // comparison: the shared runtime naturally accumulates frame history
    // while each independent runtime is intentionally cold.
    auto shared_runtime = make_renderer_shared();
    auto settings = shared_runtime->render_settings();
    settings.motion_blur.mode = MotionBlurMode::Off;
    // Dirty rects and tiles intentionally depend on the previous framebuffer.
    // Disable them here so random/reverse orders compare graph-cache output,
    // not temporal framebuffer accumulation.
    settings.dirty.enabled = false;
    settings.dirty.use_bitmask = false;
    settings.dirty.use_tiles = false;
    // Reproduce the production configuration. Diagnostics must not alter the
    // rendered result, and OFF is the known-red path for this fixture.
    settings.diagnostics.enabled = false;
    shared_runtime->set_settings(settings);

    int mismatch_count = 0;
    int first_divergent_position = -1;
    int first_divergent_frame = -1;

    // One shared runtime is intentionally used for the complete order. This
    // is the cache-under-test: every call observes the prior frame's graph
    // and node-cache state.
    for (std::size_t position = 0; position < order.size(); ++position) {
        const int frame_value = order[position];
        REQUIRE(frame_value >= kFirstFrame);
        REQUIRE(frame_value <= kLastFrame);

        auto shared_frame = shared_runtime->render(composition_to_render,
                                                   Frame{frame_value});
        REQUIRE(shared_frame != nullptr);
        REQUIRE(shared_frame->width() == kWidth);
        REQUIRE(shared_frame->height() == kHeight);
        const std::uint64_t shared_hash = framebuffer_hash(*shared_frame);
        REQUIRE_MESSAGE(has_non_black_frame(*shared_frame),
                            "shared render produced a black/empty frame at frame " << frame_value);

        // A new renderer means a new RenderRuntime, graph cache, node cache,
        // framebuffer pool and render session for this exact frame. Match the
        // shared runtime's settings so only cache state differs.
        auto independent_runtime = make_renderer_shared();
        independent_runtime->set_settings(settings);
        auto independent_frame = independent_runtime->render(
            composition_to_render, Frame{frame_value});
        REQUIRE(independent_frame != nullptr);
        REQUIRE(independent_frame->width() == kWidth);
        REQUIRE(independent_frame->height() == kHeight);
        const std::uint64_t independent_hash = framebuffer_hash(*independent_frame);
        REQUIRE_MESSAGE(has_non_black_frame(*independent_frame),
                        "independent render produced a black/empty frame at frame " << frame_value);

        const bool matches = shared_hash == independent_hash;
        if (!matches) {
            ++mismatch_count;
            if (first_divergent_position < 0) {
                first_divergent_position = static_cast<int>(position);
                first_divergent_frame = frame_value;
            }
        }

        INFO("order=", order_name,
             " position=", position,
             " frame=", frame_value,
             " shared_hash=", shared_hash,
             " independent_hash=", independent_hash,
             " first_divergent_position=", first_divergent_position,
             " first_divergent_frame=", first_divergent_frame);
    }

    INFO("order=", order_name,
         " mismatch_count=", mismatch_count,
         " first_divergent_position=", first_divergent_position,
         " first_divergent_frame=", first_divergent_frame);
    CHECK_MESSAGE(mismatch_count == 0,
                  "sequential graph-cache parity failed for order='" << order_name
                  << "' first_divergent_position=" << first_divergent_position
                  << " first_divergent_frame=" << first_divergent_frame
                  << " mismatch_count=" << mismatch_count);
}

} // namespace

TEST_CASE("Sequential graph cache verifier: opacity is exactly zero without changing topology") {
    const auto composition_to_render = make_sequential_cache_composition();
    const auto scene_at_zero = composition_to_render.evaluate(
        make_ctx(Frame{0}, kWidth, kHeight));

    const auto zero_opacity_layer = std::find_if(
        scene_at_zero.layers().begin(), scene_at_zero.layers().end(),
        [](const Layer& layer) { return layer.name == "zero_opacity"; });
    REQUIRE(zero_opacity_layer != scene_at_zero.layers().end());
    CHECK(zero_opacity_layer->transform.opacity == doctest::Approx(0.0f));
}

TEST_CASE("Sequential graph cache verifier: cold and warm renders stay stable") {
    const auto composition_to_render = make_sequential_cache_composition();
    auto renderer = make_renderer_shared();
    auto settings = renderer->render_settings();
    settings.motion_blur.mode = MotionBlurMode::Off;
    settings.dirty.enabled = false;
    settings.diagnostics.enabled = false;
    renderer->set_settings(settings);

    auto first = renderer->render(composition_to_render, Frame{0});
    REQUIRE(first != nullptr);
    const auto first_hash = framebuffer_hash(*first);

    auto second = renderer->render(composition_to_render, Frame{0});
    REQUIRE(second != nullptr);
    CHECK(framebuffer_hash(*second) == first_hash);
    CHECK(average_luma_rect(*first, 0, 0, kWidth, kHeight) > 0.0005f);
    CHECK(average_luma_rect(*second, 0, 0, kWidth, kHeight) > 0.0005f);
}

TEST_CASE("Sequential graph cache verifier: frames 0-59 match independent runtime in every order") {
    const auto composition_to_render = make_sequential_cache_composition();

    verify_order("linear", linear_order(), composition_to_render);
    verify_order("random", random_order(), composition_to_render);
    verify_order("reverse", reverse_order(), composition_to_render);
    verify_order("repeated", repeated_order(), composition_to_render);
}
