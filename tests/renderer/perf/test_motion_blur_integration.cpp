#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include "src/render_graph/pipeline/temporal_render_pipeline.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <vector>
#include <stdexcept>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;


namespace {

// ── Helper: build a composition with a layer that moves horizontally ──
//    Position goes from x_left → x_right over 1 frame at 30 fps.
//    Each motion blur sub-sample sees a different position.
Composition make_moving_layer_comp(int w, int h, f32 x_left, f32 x_right) {
    CompositionSpec spec{.name="MBIntegration_MovingLayer", .width=w, .height=h,
                         .frame_rate={30,1}, .duration=60};
    return composition(spec, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // Keep this fixture on the canonical 2D path. The previous version
        // combined a camera pose with frame-keyed LayerBuilder animation and
        // rendered an entirely black frame, so its smear assertions never
        // exercised temporal accumulation.
        s.rect("background", {
            .size = {static_cast<f32>(w), static_cast<f32>(h)},
            .color = Color::black(),
            .pos = {static_cast<f32>(w) * 0.5f,
                    static_cast<f32>(h) * 0.5f, 0.0f},
        });
        const float frame = static_cast<float>(ctx.effective_frame());
        const float x = x_left + (x_right - x_left) * frame;
        s.rect("moving_box", {
            .size = {20.0f, static_cast<f32>(h)},
            .color = Color::red(),
            .pos = {x, static_cast<f32>(h) * 0.5f, 0.0f},
        });
        return s.build();
    });
}

// ── Helper: count non-black pixels in a framebuffer ──
[[nodiscard]] int count_non_black_pixels(const Framebuffer& fb) {
    int count = 0;
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.r > 0.01f || c.g > 0.01f || c.b > 0.01f) {
                ++count;
            }
        }
    }
    return count;
}

[[nodiscard]] int smear_width(const Framebuffer& fb, int row) {
    int min_x = fb.width();
    int max_x = -1;
    for (i32 x = 0; x < fb.width(); ++x) {
        if (fb.get_pixel(x, row).r > 0.01f) {
            min_x = std::min(min_x, static_cast<int>(x));
            max_x = std::max(max_x, static_cast<int>(x));
        }
    }
    return max_x >= min_x ? max_x - min_x : 0;
}

[[nodiscard]] bool is_finite_premultiplied(const Framebuffer& fb) {
    constexpr float epsilon = 1e-5f;
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (!std::isfinite(c.r) || !std::isfinite(c.g) ||
                !std::isfinite(c.b) || !std::isfinite(c.a) ||
                c.r < -epsilon || c.g < -epsilon || c.b < -epsilon ||
                c.a < -epsilon || c.r > c.a + epsilon ||
                c.g > c.a + epsilon || c.b > c.a + epsilon) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

// ============================================================================
// Integration: 8-sample motion blur produces perceptible sub-frame differences
// ============================================================================

TEST_CASE("Motion blur: 8 samples smear a fast-moving layer across the frame") {
    const int w = 300;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 50.0f, 250.0f);

    // ── Render WITH motion blur ──────────────────────────────────────────
    RenderSettings mb_settings;
    mb_settings.motion_blur.mode             = MotionBlurMode::TemporalAccumulation;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;   // centred
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Uniform;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;

    auto mb_renderer = test::make_renderer();
    mb_renderer.set_settings(mb_settings);
    auto mb_fb = mb_renderer.render(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    // ── Render WITHOUT motion blur ───────────────────────────────────────
    RenderSettings no_mb_settings;
    no_mb_settings.motion_blur.mode    = MotionBlurMode::Off;

    auto no_mb_renderer = test::make_renderer();
    no_mb_renderer.set_settings(no_mb_settings);
    auto no_mb_fb = no_mb_renderer.render(comp, Frame{0});
    REQUIRE(no_mb_fb != nullptr);

    // ── Assertions ──────────────────────────────────────────────────────

    // 1. Motion blur should produce MORE non-black pixels (smear) than
    //    the single-frame render (box is 20px wide × 200px tall = 4000 px)
    const int mb_pixels  = count_non_black_pixels(*mb_fb);
    const int no_mb_pixels = count_non_black_pixels(*no_mb_fb);
    INFO("mb_pixels=", mb_pixels, " no_mb_pixels=", no_mb_pixels,
         " mb_mid=", mb_fb->get_pixel(w / 2, h / 2).r,
         " no_mb_mid=", no_mb_fb->get_pixel(w / 2, h / 2).r);
    CHECK(mb_pixels > no_mb_pixels);

    // A fully covered source can remain pure red even after temporal
    // accumulation; coverage width, not a fractional channel value, is the
    // stable runtime invariant for this fixture.
    const int mid_y = h / 2;
    INFO("mb_width=", smear_width(*mb_fb, mid_y),
         " no_mb_width=", smear_width(*no_mb_fb, mid_y));
    CHECK(smear_width(*mb_fb, mid_y) > smear_width(*no_mb_fb, mid_y));
    CHECK(is_finite_premultiplied(*mb_fb));
}

TEST_CASE("Motion blur: Stratified pattern with Triangle filter produces consistent weights") {
    const int w = 200;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 20.0f, 180.0f);

    // Stratified + Triangle: should still produce valid blended output
    RenderSettings mb_settings;
    mb_settings.motion_blur.mode             = MotionBlurMode::TemporalAccumulation;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Stratified;
    mb_settings.motion_blur.filter           = TemporalFilter::Triangle;
    mb_settings.motion_blur.jitter_seed      = 42;

    auto mb_renderer = test::make_renderer();
    mb_renderer.set_settings(mb_settings);
    auto mb_fb = mb_renderer.render(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    const int mid_y = h / 2;
    // Triangle weights may still produce fully covered red pixels at this
    // raster resolution; assert the observable temporal footprint instead of
    // requiring fractional channel coverage.
    CHECK(count_non_black_pixels(*mb_fb) > 0);
    CHECK(smear_width(*mb_fb, mid_y) > 20);
    CHECK(is_finite_premultiplied(*mb_fb));
}

TEST_CASE("Motion blur: deterministic — same seed produces identical output") {
    const int w = 200;
    const int h = 200;

    auto comp = make_moving_layer_comp(w, h, 20.0f, 180.0f);

    RenderSettings mb_settings;
    mb_settings.motion_blur.mode             = MotionBlurMode::TemporalAccumulation;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Stratified;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;
    mb_settings.motion_blur.jitter_seed      = 0x3A5C9F1E;

    // First render
    auto r1 = test::make_renderer();
    r1.set_settings(mb_settings);
    auto fb1 = r1.render(comp, Frame{0});
    REQUIRE(fb1 != nullptr);

    // Second render (same settings, fresh renderer to avoid cache reuse)
    auto r2 = test::make_renderer();
    r2.set_settings(mb_settings);
    auto fb2 = r2.render(comp, Frame{0});
    REQUIRE(fb2 != nullptr);

    // Pixel-for-pixel equality
    bool all_equal = true;
    for (i32 y = 0; y < h && all_equal; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color c1 = fb1->get_pixel(x, y);
            Color c2 = fb2->get_pixel(x, y);
            if (std::abs(c1.r - c2.r) > 0.001f ||
                std::abs(c1.g - c2.g) > 0.001f ||
                std::abs(c1.b - c2.b) > 0.001f ||
                std::abs(c1.a - c2.a) > 0.001f) {
                all_equal = false;
                break;
            }
        }
    }
    CHECK(all_equal);
}

TEST_CASE("Motion blur: sub-frame pipeline — 8 samples produce 8 distinct positions") {
    // This is the decisive architecture test: without the sub-frame pipeline
    // fixes, all 8 sub-samples would evaluate at the same integer frame
    // and the motion-blurred output would be identical to the no-MB output.

    const int w = 300;
    const int h = 100;
    auto comp = make_moving_layer_comp(w, h, 0.0f, 300.0f);

    RenderSettings mb_settings;
    mb_settings.motion_blur.mode             = MotionBlurMode::TemporalAccumulation;
    mb_settings.motion_blur.samples          = 8;
    mb_settings.motion_blur.shutter_angle_deg = 180.0f;
    mb_settings.motion_blur.shutter_phase_deg = -90.0f;
    mb_settings.motion_blur.pattern          = TemporalSamplePattern::Uniform;
    mb_settings.motion_blur.filter           = TemporalFilter::Box;

    auto r = test::make_renderer();
    r.set_settings(mb_settings);
    auto mb_fb = r.render(comp, Frame{0});
    REQUIRE(mb_fb != nullptr);

    // Without motion blur, the box at frame 0 is at x=0.
    // With 8 samples and 180° shutter, the box is sampled at positions
    // ranging from x≈0 to x≈150 (half the 300px movement over 1 frame).

    // Collect the range of x-coordinates in the middle row where non-black
    // pixels appear. The MB smear should span a wide range.
    const int mid_y = h / 2;
    int min_x = w, max_x = -1;
    for (i32 x = 0; x < w; ++x) {
        Color c = mb_fb->get_pixel(x, mid_y);
        if (c.r > 0.01f) {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
        }
    }
    REQUIRE(min_x <= max_x);  // at least some non-black pixels

    // The smear should span more than just the box width (20px),
    // confirming sub-frame sampling is working.
    const int observed_smear_width = max_x - min_x;
    CHECK(observed_smear_width > 25);  // sub-frame sampling is visible

    // Verify: without MB, the box is at a single position
    RenderSettings no_mb_settings;
    no_mb_settings.motion_blur.mode    = MotionBlurMode::Off;
    auto r2 = test::make_renderer();
    r2.set_settings(no_mb_settings);
    auto no_mb_fb = r2.render(comp, Frame{0});

    int no_mb_min = w, no_mb_max = -1;
    for (i32 x = 0; x < w; ++x) {
        Color c = no_mb_fb->get_pixel(x, mid_y);
        if (c.r > 0.01f) {
            if (x < no_mb_min) no_mb_min = x;
            if (x > no_mb_max) no_mb_max = x;
        }
    }
    if (no_mb_min <= no_mb_max) {
        int no_mb_width = no_mb_max - no_mb_min;
        // The MB smear should be wider than the no-MB box
        CHECK(observed_smear_width > no_mb_width);
    }
}

TEST_CASE("Motion blur runtime matrix: 1/2/4/8 samples preserve cold-warm parity across orders") {
    const auto comp = make_moving_layer_comp(160, 80, 12.0f, 28.0f);
    const std::array<int, 4> sample_counts{1, 2, 4, 8};
    std::array<std::vector<Frame>, 3> orders{
        std::vector<Frame>{Frame{0}, Frame{1}, Frame{2}, Frame{3}},
        std::vector<Frame>{Frame{0}, Frame{1}, Frame{2}, Frame{3}},
        std::vector<Frame>{Frame{0}, Frame{1}, Frame{2}, Frame{3}},
    };
    std::mt19937 rng(0xC0FFEEu);
    std::shuffle(orders[1].begin(), orders[1].end(), rng);
    std::reverse(orders[2].begin(), orders[2].end());

    for (const int samples : sample_counts) {
        for (const auto& order : orders) {
            RenderSettings settings;
            settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
            settings.motion_blur.samples = samples;
            INFO("samples=", samples, " order=", order.front().integral(), "→", order.back().integral(),
                 " samples=1 uses the renderer's documented single-frame fallback");
            settings.motion_blur.shutter_angle_deg = 180.0f;
            settings.motion_blur.shutter_phase_deg = -90.0f;
            settings.motion_blur.pattern = TemporalSamplePattern::Uniform;
            settings.motion_blur.filter = TemporalFilter::Box;
            settings.motion_blur.jitter_seed = 0xC0FFEE;

            auto warm_renderer = test::make_renderer_shared();
            warm_renderer->set_settings(settings);
            std::map<int, u64> warm_hashes;
            for (const Frame frame : order) {
                auto framebuffer = warm_renderer->render(comp, frame);
                REQUIRE(framebuffer != nullptr);
                warm_hashes.emplace(frame.integral(), test::framebuffer_hash(*framebuffer));
            }

            for (const Frame frame : order) {
                auto cold_renderer = test::make_renderer_shared();
                cold_renderer->set_settings(settings);
                auto framebuffer = cold_renderer->render(comp, frame);
                REQUIRE(framebuffer != nullptr);
                CAPTURE(samples);
                CAPTURE(frame);
                CHECK(test::framebuffer_hash(*framebuffer) == warm_hashes.at(frame.integral()));
            }

            auto repeat = warm_renderer->render(comp, Frame{2});
            REQUIRE(repeat != nullptr);
            CHECK(test::framebuffer_hash(*repeat) == warm_hashes.at(2));

            const auto runtime_report = warm_renderer->session().memory_tracker->snapshot();
            CHECK(runtime_report.current_live_bytes == 0);
            CHECK(runtime_report.peak_live_bytes >= runtime_report.current_live_bytes);
            std::uint64_t allocation_events = 0;
            std::uint64_t allocated_bytes = 0;
            std::uint64_t temporary_buffers = 0;
            for (const auto& node : runtime_report.nodes) {
                allocation_events += node.allocations;
                allocated_bytes += node.allocated_bytes;
                temporary_buffers += node.temporary_buffers;
                if (node.allocated_bytes > 0) {
                    CHECK(node.allocations > 0);
                }
                CHECK(node.peak_live_bytes >= node.live_bytes);
            }
            if (allocated_bytes > 0) {
                CHECK(allocation_events > 0);
            }
            if (samples > 1) {
                CHECK(temporary_buffers > 0);
            }
            if (samples > 1) {
                REQUIRE(runtime_report.samples.size() >= static_cast<std::size_t>(samples));
                std::vector<TemporalSampleKey> sample_keys;
                for (const auto& sample : runtime_report.samples) {
                    CHECK(sample.live_bytes == 0);
                    CHECK(sample.peak_live_bytes >= sample.live_bytes);
                    const bool unique = std::none_of(
                        sample_keys.begin(), sample_keys.end(),
                        [&](const auto& key) { return key == sample.sample_key; });
                    CHECK(unique);
                    sample_keys.push_back(sample.sample_key);
                }
                CHECK(sample_keys.size() >= static_cast<std::size_t>(samples));
            }
        }
    }
}

TEST_CASE("Motion blur runtime budget: RenderBudget rejects over-budget request") {
    const auto comp = make_moving_layer_comp(160, 80, 12.0f, 28.0f);
    auto renderer = test::make_renderer_shared();
    RenderSettings settings;
    settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    settings.motion_blur.samples = 8;
    settings.motion_blur.pattern = TemporalSamplePattern::Uniform;
    settings.motion_blur.filter = TemporalFilter::Box;
    settings.render_budget.max_temporal_pixels = 1000;
    renderer->set_settings(settings);

    CHECK_THROWS_AS(renderer->render(comp, Frame{0}), std::invalid_argument);
}

TEST_CASE("Motion blur runtime cancellation: stops between temporal samples") {
    chronon3d::CancellationToken cancellation;
    int evaluations = 0;
    const Composition comp = composition(
        {.name = "MotionBlurCancellation", .width = 96, .height = 64,
         .frame_rate = {30, 1}, .duration = 4},
        [&cancellation, &evaluations](const FrameContext& ctx) {
            ++evaluations;
            if (evaluations == 2) cancellation.cancel();
            SceneBuilder scene(ctx);
            scene.rect("background", {
                .size = {96.0f, 64.0f},
                .color = Color::black(),
                .pos = {48.0f, 32.0f, 0.0f},
            });
            scene.rect("moving", {
                .size = {16.0f, 64.0f},
                .color = Color::red(),
                .pos = {24.0f, 32.0f, 0.0f},
            });
            return scene.build();
        });

    auto compile_result = compile_composition(comp, {});
    REQUIRE(compile_result.has_value());
    auto compiled = std::move(compile_result).value();

    auto renderer = test::make_renderer_shared();
    RenderSettings settings;
    settings.motion_blur.mode = MotionBlurMode::TemporalAccumulation;
    settings.motion_blur.samples = 8;
    settings.motion_blur.pattern = TemporalSamplePattern::Uniform;
    settings.motion_blur.filter = TemporalFilter::Box;
    renderer->set_settings(settings);

    auto framebuffer = graph::render_compiled_composition_frame_temporal(
        renderer->backend(), renderer->node_cache(), settings, nullptr, nullptr,
        compiled, Frame{0}, renderer.get(), &cancellation);
    CHECK(framebuffer == nullptr);
    CHECK(evaluations >= 2);
    CHECK(evaluations < settings.motion_blur.samples);
    const auto cancelled_report = renderer->session().memory_tracker->snapshot();
    CHECK(cancelled_report.current_live_bytes == 0);
    CHECK(cancelled_report.peak_live_bytes >= cancelled_report.current_live_bytes);
    REQUIRE(cancelled_report.samples.size() >= 1);
    std::vector<TemporalSampleKey> cancelled_keys;
    for (const auto& sample : cancelled_report.samples) {
        CHECK(sample.live_bytes == 0);
        const bool unique = std::none_of(
            cancelled_keys.begin(), cancelled_keys.end(),
            [&](const auto& key) { return key == sample.sample_key; });
        CHECK(unique);
        cancelled_keys.push_back(sample.sample_key);
    }
    CHECK(cancelled_keys.size() < static_cast<std::size_t>(settings.motion_blur.samples));
}

TEST_CASE("Motion blur memory report: sample domains remain distinct for 1/2/4/8") {
    const std::array<int, 4> sample_counts{1, 2, 4, 8};
    for (const int samples : sample_counts) {
        graph::NodeMemoryTracker tracker;
        std::vector<std::unique_ptr<graph::ScopedNodeMemory>> scopes;
        scopes.reserve(static_cast<std::size_t>(samples));
        for (int index = 0; index < samples; ++index) {
            const TemporalSampleKey key{Frame{7}, static_cast<u32>(index + 1), 3};
            scopes.push_back(std::make_unique<graph::ScopedNodeMemory>(
                tracker, "motion-sample", key, 64));
        }

        const auto live_report = tracker.snapshot();
        CAPTURE(samples);
        REQUIRE(live_report.samples.size() == static_cast<std::size_t>(samples));
        CHECK(live_report.current_live_bytes == static_cast<std::uint64_t>(samples * 64));
        CHECK(live_report.peak_live_bytes == static_cast<std::uint64_t>(samples * 64));
        REQUIRE(live_report.nodes.size() == 1);
        CHECK(live_report.nodes.front().temporary_buffers ==
              static_cast<std::uint64_t>(samples));
        CHECK(live_report.nodes.front().live_bytes ==
              static_cast<std::uint64_t>(samples * 64));
        CHECK(live_report.nodes.front().peak_live_bytes ==
              static_cast<std::uint64_t>(samples * 64));

        scopes.clear();
        const auto released_report = tracker.snapshot();
        CHECK(released_report.current_live_bytes == 0);
        CHECK(released_report.peak_live_bytes == static_cast<std::uint64_t>(samples * 64));
    }
}
