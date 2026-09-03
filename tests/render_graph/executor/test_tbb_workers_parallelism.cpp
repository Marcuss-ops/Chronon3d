// Runtime regression for actual TBB worker usage in the render-graph path.

#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
using namespace chronon3d;
using namespace chronon3d::test;

namespace {

const std::string IMAGE_PATH = "assets/images/minimalist_landscape.png";
const Vec2 IMAGE_SIZE = {800.0f, 450.0f};

void add_common_background(SceneBuilder& s) {
    s.layer("background", [](auto& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = {1920.0f, 1080.0f},
            .offset = {0.0f, 0.0f},
            .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
            .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
            .spacing = 136.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

void add_image_border(LayerBuilder& l, Vec2 size) {
    l.rounded_rect("image_backdrop", {
        .size = size + Vec2{24.0f, 24.0f},
        .radius = 16.0f,
        .color = Color{0.0f, 0.0f, 0.0f, 0.35f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
    l.rounded_rect("image_border", {
        .size = size + Vec2{2.0f, 2.0f},
        .radius = 10.0f,
        .color = Color{0.25f, 0.27f, 0.31f, 0.8f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
}

Composition make_parallelism_fixture() {
    return composition(
        {.name = "TbbParallelismFixture", .duration = 150},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s);
            s.layer("image_layer", [](auto& l) {
                l.pin_to(Anchor::Center);
                l.motion("tracking_breathing", {.scale = 1.04f, .duration = Frame{120}});
                add_image_border(l, IMAGE_SIZE);
                l.image("img", {
                    .asset_path = IMAGE_PATH,
                    .size = IMAGE_SIZE,
                    .radius = 8.0f
                });
            });
            return s.build();
        }
    );
}

} // namespace

TEST_CASE("TBB executor uses multiple workers when parallel regions execute") {
    REQUIRE_MESSAGE(std::filesystem::exists(IMAGE_PATH),
                    "Asset not found: " << IMAGE_PATH);

    int peak_workers = 0;
    int64_t avg_sum = 0;
    int64_t avg_cnt = 0;
    uint64_t parallel_regions = 0;
    uint64_t sequential_levels = 0;

    for (int frame_num = 0; frame_num < 3; ++frame_num) {
        auto renderer = test::make_renderer();
        auto* counters = renderer.counters();
        REQUIRE(counters != nullptr);

        auto fb = renderer.render(make_parallelism_fixture(), Frame{50 + frame_num});
        REQUIRE(fb != nullptr);

        const uint64_t peak = counters->tbb_active_workers_peak.load(std::memory_order_relaxed);
        const int64_t sum = counters->tbb_active_workers_avg_sum.load(std::memory_order_relaxed);
        const int64_t count = counters->tbb_active_workers_avg_count.load(std::memory_order_relaxed);
        const uint64_t regions = counters->parallel_regions_count.load(std::memory_order_relaxed);
        const uint64_t sequential = counters->level_sequential_count.load(std::memory_order_relaxed);

        peak_workers = std::max(peak_workers, static_cast<int>(peak));
        avg_sum = std::max(avg_sum, sum);
        avg_cnt = std::max(avg_cnt, count);
        parallel_regions = std::max(parallel_regions, regions);
        sequential_levels = std::max(sequential_levels, sequential);
    }

    INFO("peak_workers=" << peak_workers
         << " parallel_regions=" << parallel_regions
         << " sequential_levels=" << sequential_levels
         << " avg_count=" << avg_cnt
         << " avg_sum=" << avg_sum);

    CHECK(parallel_regions > 0);
    CHECK(peak_workers > 1);
}
