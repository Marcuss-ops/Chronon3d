// test_tbb_workers_parallelism.cpp
//
// Rende MinimalistImageTrackingBreathing frame 50 e verifica che
// tbb_active_workers_peak > 1 (il parallelismo TBB usa davvero più worker).
//
// Uso: ./chronon3d_tbb_workers_test

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cstdio>
#include <cstdlib>
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

Composition make_breathing_comp() {
    return composition(
        {.name = "MinimalistImageTrackingBreathing", .duration = 150},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s);
            s.layer("image_layer", [](auto& l) {
                l.pin_to(Anchor::Center);
                l.tracking_breathing(1.04f, Frame{120});
                add_image_border(l, IMAGE_SIZE);
                l.image("img", {
                    .path = IMAGE_PATH,
                    .size = IMAGE_SIZE,
                    .radius = 8.0f
                });
            });
            return s.build();
        }
    );
}

} // anonymous namespace

int main() {
    if (!std::filesystem::exists(IMAGE_PATH)) {
        std::fprintf(stderr, "ERROR: Asset not found: %s\n", IMAGE_PATH.c_str());
        return 1;
    }

    std::printf("=== TBB Workers Parallelism Test ===\n");
    std::printf("Rendering MinimalistImageTrackingBreathing frame 50...\n\n");

    // Render TWO frames to accumulate parallelism counters across multiple
    // execute_levels() calls.  The first frame sets up caches; the second
    // frame exercises the parallel node execution paths fully.
    int peak_workers = 0;
    int64_t avg_sum = 0;
    int64_t avg_cnt = 0;
    uint64_t parallel_regions = 0;
    uint64_t sequential_levels = 0;
    int64_t tbb_spawn_count = 0;

    for (int frame_num = 0; frame_num < 3; ++frame_num) {
        auto renderer = test::make_renderer();
        auto* counters = renderer.counters();
        if (!counters) {
            std::fprintf(stderr, "ERROR: No counters available\n");
            return 1;
        }

        // Render frame at frame 50 + frame_num (breathing animation changes slightly)
        auto fb = renderer.render(make_breathing_comp(), Frame{50 + frame_num});
        if (!fb) {
            std::fprintf(stderr, "ERROR: Frame %d returned null framebuffer\n", frame_num);
            return 1;
        }

        const uint64_t p = counters->tbb_active_workers_peak.load(std::memory_order_relaxed);
        const int64_t s = counters->tbb_active_workers_avg_sum.load(std::memory_order_relaxed);
        const int64_t c = counters->tbb_active_workers_avg_count.load(std::memory_order_relaxed);
        const uint64_t r = counters->parallel_regions_count.load(std::memory_order_relaxed);
        const uint64_t seq = counters->level_sequential_count.load(std::memory_order_relaxed);

        // tbb_worker_count doesn't exist in counters.hpp — we use what we have
        const uint64_t arena = counters->tbb_arena_max_concurrency.load(std::memory_order_relaxed);

        std::printf("  Frame %d:\n", frame_num);
        std::printf("    tbb_active_workers_peak = %lu\n", static_cast<unsigned long>(p));
        std::printf("    tbb_active_workers_avg_sum = %ld, avg_count = %ld\n",
                    static_cast<long>(s), static_cast<long>(c));
        std::printf("    tbb_arena_max_concurrency = %lu\n", static_cast<unsigned long>(arena));
        std::printf("    parallel_regions_count = %lu\n", static_cast<unsigned long>(r));
        std::printf("    level_sequential_count = %lu\n", static_cast<unsigned long>(seq));

        if (static_cast<int>(p) > peak_workers) peak_workers = static_cast<int>(p);
        if (s > avg_sum) avg_sum = s;
        if (c > avg_cnt) avg_cnt = c;
        if (r > parallel_regions) parallel_regions = r;
        if (seq > sequential_levels) sequential_levels = seq;
    }

    std::printf("\n=== Results ===\n");
    std::printf("  Peak workers across all frames: %d\n", peak_workers);
    std::printf("  Total parallel regions: %lu\n", static_cast<unsigned long>(parallel_regions));
    std::printf("  Total sequential levels: %lu\n", static_cast<unsigned long>(sequential_levels));

    if (avg_cnt > 0) {
        const double avg_workers = static_cast<double>(avg_sum) / static_cast<double>(avg_cnt);
        std::printf("  Avg active workers (across all samples): %.2f\n", avg_workers);
    } else {
        std::printf("  Avg workers: N/A (no samples recorded)\n");
    }

    // PASS/FAIL: peak workers should be > 1 when parallel regions > 0
    bool pass = true;
    if (parallel_regions == 0) {
        std::printf("\n  ✗ FAIL: No parallel regions detected (parallel_regions_count = 0)\n");
        pass = false;
    }
    if (peak_workers <= 1) {
        std::printf("\n  ✗ FAIL: tbb_active_workers_peak = %d (expected > 1)\n", peak_workers);
        std::printf("    TBB parallel_for is not using multiple workers.\n");
        std::printf("    Possible causes:\n");
        std::printf("      - counters->reset() between frames clears the peak\n");
        std::printf("      - TBB auto_partitioner doesn't split small ranges\n");
        std::printf("      - Nested parallelism (executor + node) throttles workers\n");
        pass = false;
    }
    if (avg_cnt == 0 && parallel_regions > 0) {
        std::printf("\n  ⚠ WARNING: tbb_active_workers_avg_count = 0 despite %lu parallel regions\n",
                    static_cast<unsigned long>(parallel_regions));
        std::printf("    The worker tracking code inside parallel_for may not be executing.\n");
    }

    if (pass) {
        std::printf("\n  ✓ PASS: TBB uses multiple workers (peak=%d, regions=%lu, sequential=%lu)\n",
                    peak_workers,
                    static_cast<unsigned long>(parallel_regions),
                    static_cast<unsigned long>(sequential_levels));
    }

    return pass ? 0 : 1;
}
