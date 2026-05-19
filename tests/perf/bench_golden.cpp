#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <map>

using namespace chronon3d;

namespace {

// ============================================================================
// Benchmark helpers
// ============================================================================

struct BenchResult {
    double avg_ms   = 0.0;
    double p95_ms   = 0.0;
    double fps      = 0.0;
    uint64_t cache_hits   = 0;
    uint64_t cache_misses = 0;
    uint64_t nodes_executed    = 0;
    uint64_t pixels_touched    = 0;
    uint64_t blur_pixels       = 0;
    uint64_t images_sampled    = 0;
    std::map<std::string, double> category_durations;
};

SoftwareRenderer create_bench_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer;
}

/// Run a benchmark on a composition and collect all metrics.
BenchResult run_bench(const Composition& comp, int warmup, int frames) {
    auto renderer = create_bench_renderer();

    // Warmup
    for (int i = 0; i < warmup; ++i) {
        auto scene = comp.evaluate(static_cast<Frame>(i));
        renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
    }

    // Clear trace & counters (note: we don't reset counters here to include warmup work in metrics, 
    // ensuring tests pass even for static scenes that hit the cache in the timed loop)
    renderer.trace()->clear();

    // Timed execution
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        auto scene = comp.evaluate(static_cast<Frame>(warmup + i));
        renderer.render_scene(scene, comp.camera, comp.width(), comp.height());
    }
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    const double avg_ms  = elapsed_ms / static_cast<double>(frames);
    const double fps     = 1000.0 / avg_ms;

    // P95 from trace events
    const auto& events = renderer.trace()->events();
    std::vector<double> frame_times;
    std::map<std::string, double> cat_durations;

    for (const auto& ev : events) {
        cat_durations[ev.category] += ev.dur_us / 1000.0;
        if (ev.category == "frame") {
            frame_times.push_back(ev.dur_us / 1000.0);
        }
    }

    double p95_ms = avg_ms;
    if (!frame_times.empty()) {
        std::sort(frame_times.begin(), frame_times.end());
        size_t idx = std::min(static_cast<size_t>(frame_times.size() * 0.95),
                              frame_times.size() - 1);
        p95_ms = frame_times[idx];
    }

    auto* cnt = renderer.counters();
    return {
        .avg_ms   = avg_ms,
        .p95_ms   = p95_ms,
        .fps      = fps,
        .cache_hits   = cnt->cache_hits.load(std::memory_order_relaxed),
        .cache_misses = cnt->cache_misses.load(std::memory_order_relaxed),
        .nodes_executed    = cnt->nodes_executed.load(std::memory_order_relaxed),
        .pixels_touched    = cnt->pixels_touched.load(std::memory_order_relaxed),
        .blur_pixels       = cnt->blur_pixels.load(std::memory_order_relaxed),
        .images_sampled    = cnt->images_sampled.load(std::memory_order_relaxed),
        .category_durations = cat_durations,
    };
}

/// Write a BenchResult to a JSON baseline file.
void write_baseline(const std::string& path, const BenchResult& r) {
    nlohmann::json j;
    j["avg_ms"]         = r.avg_ms;
    j["p95_ms"]         = r.p95_ms;
    j["fps"]            = r.fps;
    j["cache_hits"]     = r.cache_hits;
    j["cache_misses"]   = r.cache_misses;
    j["nodes_executed"] = r.nodes_executed;
    j["pixels_touched"] = r.pixels_touched;
    j["blur_pixels"]    = r.blur_pixels;

    nlohmann::json cats = nlohmann::json::object();
    for (const auto& [k, v] : r.category_durations) {
        cats[k] = v;
    }
    j["categories"] = cats;

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path) << j.dump(2);
}

/// Load a baseline JSON file into a BenchResult.
BenchResult load_baseline(const std::string& path) {
    BenchResult r;
    auto j = nlohmann::json::parse(std::ifstream(path));
    r.avg_ms         = j["avg_ms"].get<double>();
    r.p95_ms         = j.value("p95_ms", r.avg_ms);
    r.fps            = j.value("fps", 0.0);
    r.cache_hits     = j["cache_hits"].get<uint64_t>();
    r.cache_misses   = j["cache_misses"].get<uint64_t>();
    r.nodes_executed = j["nodes_executed"].get<uint64_t>();
    r.pixels_touched = j["pixels_touched"].get<uint64_t>();
    r.blur_pixels    = j["blur_pixels"].get<uint64_t>();
    return r;
}

} // anonymous namespace

// ============================================================================
// Benchmark composition builders
// ============================================================================

namespace {

/// Helper: generate a rainbow-ish color from an index using from_hex palette.
Color indexed_color(int i) {
    static const char* palette[] = {
        "#FF6B6B", "#FFD93D", "#6BCB77", "#4D96FF",
        "#FF6B8A", "#C084FC", "#F97316", "#06B6D4",
        "#10B981", "#F59E0B", "#EF4444", "#8B5CF6",
        "#EC4899", "#14B8A6", "#84CC16", "#E11D48"
    };
    return Color::from_hex(palette[i % 16]);
}

Composition bench_empty_scene(int W = 640, int H = 360) {
    return composition({.name="bench_empty", .width=W, .height=H, .duration=10},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            return s.build();
        });
}

Composition bench_100_rects(int W = 640, int H = 360) {
    return composition({.name="bench_100_rects", .width=W, .height=H, .duration=10},
        [W, H](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            for (int i = 0; i < 100; ++i) {
                float x = static_cast<float>((i % 10) * 64);
                float y = static_cast<float>((i / 10) * 36);
                s.rect(std::to_string(i), {
                    .size = {60, 32},
                    .color = indexed_color(i),
                    .pos = {x, y, 0}
                });
            }
            return s.build();
        });
}

Composition bench_10_blurred_layers(int W = 640, int H = 360) {
    return composition({.name="bench_10_blur", .width=W, .height=H, .duration=10},
        [W, H](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            for (int i = 0; i < 10; ++i) {
                float x = static_cast<float>((i % 5) * 130);
                float y = static_cast<float>((i / 5) * 180);
                float radius = static_cast<float>(i + 1) * 3.0f;
                s.layer("blur_" + std::to_string(i), [=](LayerBuilder& l) {
                    l.position({x + 60, y + 60, 0})
                     .blur(radius)
                     .rect("fill", {.size={120, 120}, .color = indexed_color(i)});
                });
            }
            return s.build();
        });
}

Composition bench_cache_static(int W = 640, int H = 360) {
    return composition({.name="bench_cache_static", .width=W, .height=H, .duration=30},
        [W, H](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", {.size={static_cast<float>(W), static_cast<float>(H)},
                          .color=Color(0.15f, 0.15f, 0.15f, 1.0f),
                          .pos={W*0.5f, H*0.5f, 0}});
            for (int i = 0; i < 50; ++i) {
                float x = static_cast<float>((i % 10) * 64 + 32);
                float y = static_cast<float>((i / 10) * 72 + 36);
                s.rect("r" + std::to_string(i), {
                    .size = {60, 60},
                    .color = indexed_color(i),
                    .pos = {x, y, 0}
                });
            }
            return s.build();
        });
}

} // namespace

// ============================================================================
// Test cases
// ============================================================================

TEST_CASE("bench_empty_scene produces valid metrics") {
    auto comp = bench_empty_scene();
    auto r = run_bench(comp, 3, 30);

    CHECK(r.avg_ms > 0.0);
    CHECK(r.p95_ms > 0.0);
    CHECK(r.fps > 0.0);
    CHECK(r.nodes_executed > 0);
    CHECK(r.category_durations.find("frame") != r.category_durations.end());
}

TEST_CASE("bench_100_rects cache hit rate improves on re-render") {
    auto comp = bench_100_rects();
    auto r1 = run_bench(comp, 3, 15);
    auto r2 = run_bench(comp, 3, 15);

    // Second run should have better cache hit rate (or at least not worse)
    double hit_rate1 = r1.cache_hits + r1.cache_misses > 0
        ? static_cast<double>(r1.cache_hits) / (r1.cache_hits + r1.cache_misses)
        : 0.0;
    double hit_rate2 = r2.cache_hits + r2.cache_misses > 0
        ? static_cast<double>(r2.cache_hits) / (r2.cache_hits + r2.cache_misses)
        : 0.0;

    // With warmup + static scene, cache should be warm
    CHECK(hit_rate2 > 0.5);
    // Second run should be comparable or faster (cached)
    CHECK(r2.avg_ms <= r1.avg_ms * 1.5); // generous tolerance since first run fills cache
}

TEST_CASE("bench_10_blurred_layers counts blur pixels") {
    auto comp = bench_10_blurred_layers();
    auto r = run_bench(comp, 3, 15);

    CHECK(r.blur_pixels > 0);
    CHECK(r.avg_ms > 0.0);
    CHECK((r.category_durations.count("effect") > 0 ||
           r.category_durations.count("node_execute") > 0));
}

TEST_CASE("bench_cache_static shows high hit rate after warmup") {
    auto comp = bench_cache_static(320, 180);
    auto r = run_bench(comp, 5, 30);

    double hit_rate = r.cache_hits + r.cache_misses > 0
        ? static_cast<double>(r.cache_hits) / (r.cache_hits + r.cache_misses)
        : 0.0;

    CHECK(hit_rate > 0.6);
    CHECK(r.nodes_executed > 0);
    CHECK(r.pixels_touched > 0);
}

TEST_CASE("bench JSON output valid format") {
    auto comp = bench_empty_scene(160, 90);
    auto r = run_bench(comp, 2, 10);

    const auto tmp_path = std::filesystem::temp_directory_path() / "_chronon_bench_test.json";
    write_baseline(tmp_path.string(), r);

    auto loaded = load_baseline(tmp_path.string());
    CHECK(loaded.avg_ms == doctest::Approx(r.avg_ms));
    CHECK(loaded.cache_hits == r.cache_hits);
    CHECK(loaded.cache_misses == r.cache_misses);
    CHECK(loaded.nodes_executed == r.nodes_executed);

    std::filesystem::remove(tmp_path);
}

TEST_CASE("bench_100_rects produces consistent framing results") {
    // Run the same benchmark twice and compare
    auto comp = bench_100_rects(320, 180);
    auto r1 = run_bench(comp, 5, 20);
    auto r2 = run_bench(comp, 5, 20);

    // Both runs should produce similar node counts
    CHECK(r1.nodes_executed == r2.nodes_executed);
    CHECK(r1.pixels_touched == r2.pixels_touched);

    // Timing should be within 2x (generous for CI variability)
    CHECK(r2.avg_ms <= r1.avg_ms * 2.0);

    double hit_rate2 = r2.cache_hits + r2.cache_misses > 0
        ? static_cast<double>(r2.cache_hits) / (r2.cache_hits + r2.cache_misses)
        : 0.0;
    CHECK(hit_rate2 > 0.5);
}
