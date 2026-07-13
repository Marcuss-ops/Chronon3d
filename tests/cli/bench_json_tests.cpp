#include <doctest/doctest.h>
#include <chronon3d/core/profiling/benchmark_report.hpp>
#include <nlohmann/json.hpp>
using namespace chronon3d;


TEST_CASE("benchmark_report to_json produces correct schema") {
    BenchmarkReport report;
    report.comp_id = "TestComp";
    report.width = 1920;
    report.height = 1080;
    report.frames = 120;
    report.warmup = 10;
    report.modular_graph = true;
    report.metrics.time_to_first_frame_ms = 120.0;
    report.metrics.avg_frame_ms = 14.5;
    report.metrics.median_frame_ms = 13.8;
    report.metrics.min_frame_ms = 12.0;
    report.metrics.max_frame_ms = 25.0;
    report.metrics.p50_frame_ms = 13.8;
    report.metrics.p95_frame_ms = 18.2;
    report.metrics.p99_frame_ms = 21.0;
    report.metrics.fps = 68.9;
    report.metrics.fps_steady_state = 70.1;
    report.memory.peak_rss_mb = 438.0;
    report.memory.peak_framebuffer_bytes = 268435456.0;
    report.memory.allocations_per_frame = 0.0;
    report.memory.bytes_copied_per_frame = 33554432.0;
    report.quality.deterministic_hash = "aabbccdd";
    report.quality.ssim = 0.9985;
    report.counters.cache_hits = 1000;
    report.counters.cache_misses = 50;
    report.counters.cache_hit_rate = 0.95;
    report.counters.nodes_executed = 3000;
    report.counters.pixels_touched = 1000000;
    report.counters.blur_pixels = 200000;
    report.counters.images_sampled = 500000;
    report.counters.text_glyphs_rasterized = 100;
    report.counters.framebuffer_copies = 12;
    report.counters.framebuffer_clears = 7;
    report.counters.full_frame_passes = 3;
    report.categories_ms["graph"] = 120.0;
    report.categories_ms["raster"] = 900.0;

    auto js = to_json(report, false);

    CHECK(js["schema"] == "chronon3d.bench.v3");
    CHECK(js["comp_id"] == "TestComp");
    CHECK(js["render"]["width"] == 1920);
    CHECK(js["render"]["height"] == 1080);
    CHECK(js["render"]["frames"] == 120);
    CHECK(js["render"]["warmup"] == 10);
    CHECK(js["render"]["modular_graph"] == true);

    CHECK(js["metrics"]["time_to_first_frame_ms"] == doctest::Approx(120.0));
    CHECK(js["metrics"]["avg_frame_ms"] == doctest::Approx(14.5));
    CHECK(js["metrics"]["median_frame_ms"] == doctest::Approx(13.8));
    CHECK(js["metrics"]["min_frame_ms"] == doctest::Approx(12.0));
    CHECK(js["metrics"]["max_frame_ms"] == doctest::Approx(25.0));
    CHECK(js["metrics"]["p50_frame_ms"] == doctest::Approx(13.8));
    CHECK(js["metrics"]["p95_frame_ms"] == doctest::Approx(18.2));
    CHECK(js["metrics"]["p99_frame_ms"] == doctest::Approx(21.0));
    CHECK(js["metrics"]["fps"] == doctest::Approx(68.9));
    CHECK(js["metrics"]["fps_steady_state"] == doctest::Approx(70.1));

    CHECK(js["memory"]["peak_rss_mb"] == doctest::Approx(438.0));
    CHECK(js["memory"]["peak_framebuffer_bytes"] == doctest::Approx(268435456.0));
    CHECK(js["memory"]["allocations_per_frame"] == doctest::Approx(0.0));
    CHECK(js["memory"]["bytes_copied_per_frame"] == doctest::Approx(33554432.0));

    CHECK(js["quality"]["deterministic_hash"] == "aabbccdd");
    CHECK(js["quality"]["ssim"] == doctest::Approx(0.9985));

    CHECK(js["counters"]["cache_hits"] == 1000);
    CHECK(js["counters"]["cache_misses"] == 50);
    CHECK(js["counters"]["cache_hit_rate"] == doctest::Approx(0.95));
    CHECK(js["counters"]["nodes_executed"] == 3000);

    CHECK(js["categories_ms"]["graph"] == doctest::Approx(120.0));
    CHECK(js["categories_ms"]["raster"] == doctest::Approx(900.0));

    CHECK(!js.contains("frame_times_ms"));
}

TEST_CASE("benchmark_report to_json includes frame_times when requested") {
    BenchmarkReport report;
    report.comp_id = "TestComp";
    report.frame_times_ms = {10.0, 12.0, 11.5};

    auto js_with = to_json(report, true);
    CHECK(js_with.contains("frame_times_ms"));
    CHECK(js_with["frame_times_ms"].size() == 3);
    CHECK(js_with["frame_times_ms"][0] == doctest::Approx(10.0));

    auto js_without = to_json(report, false);
    CHECK(!js_without.contains("frame_times_ms"));
}

TEST_CASE("benchmark_report roundtrip from_json") {
    BenchmarkReport original;
    original.comp_id = "RoundtripComp";
    original.width = 1280;
    original.height = 720;
    original.frames = 60;
    original.warmup = 5;
    original.modular_graph = false;
    original.metrics.time_to_first_frame_ms = 100.0;
    original.metrics.avg_frame_ms = 20.0;
    original.metrics.p50_frame_ms = 19.0;
    original.metrics.p95_frame_ms = 30.0;
    original.metrics.p99_frame_ms = 35.0;
    original.metrics.fps_steady_state = 48.0;
    original.memory.peak_rss_mb = 256.0;
    original.memory.allocations_per_frame = 1.5;
    original.quality.deterministic_hash = "deadbeef";
    original.quality.ssim = 0.9999;
    original.counters.cache_hits = 500;
    original.counters.cache_misses = 25;
    original.counters.framebuffer_copies = 5;
    original.counters.framebuffer_clears = 2;
    original.counters.full_frame_passes = 1;
    original.categories_ms["composite"] = 300.0;
    original.frame_times_ms = {15.0, 16.0, 17.0};

    auto js = to_json(original, true);
    auto loaded = benchmark_report_from_json(js);

    CHECK(loaded.schema == "chronon3d.bench.v3");
    CHECK(loaded.comp_id == "RoundtripComp");
    CHECK(loaded.width == 1280);
    CHECK(loaded.height == 720);
    CHECK(loaded.frames == 60);
    CHECK(loaded.warmup == 5);
    CHECK(loaded.modular_graph == false);
    CHECK(loaded.metrics.time_to_first_frame_ms == doctest::Approx(100.0));
    CHECK(loaded.metrics.avg_frame_ms == doctest::Approx(20.0));
    CHECK(loaded.metrics.p50_frame_ms == doctest::Approx(19.0));
    CHECK(loaded.metrics.p95_frame_ms == doctest::Approx(30.0));
    CHECK(loaded.metrics.p99_frame_ms == doctest::Approx(35.0));
    CHECK(loaded.metrics.fps_steady_state == doctest::Approx(48.0));
    CHECK(loaded.memory.peak_rss_mb == doctest::Approx(256.0));
    CHECK(loaded.memory.allocations_per_frame == doctest::Approx(1.5));
    CHECK(loaded.quality.deterministic_hash == "deadbeef");
    CHECK(loaded.quality.ssim == doctest::Approx(0.9999));
    CHECK(loaded.counters.cache_hits == 500);
    CHECK(loaded.counters.cache_misses == 25);
    CHECK(loaded.counters.framebuffer_copies == 5);
    CHECK(loaded.counters.framebuffer_clears == 2);
    CHECK(loaded.counters.full_frame_passes == 1);
    CHECK(loaded.categories_ms["composite"] == doctest::Approx(300.0));
    REQUIRE(loaded.frame_times_ms.size() == 3);
    CHECK(loaded.frame_times_ms[0] == doctest::Approx(15.0));
}

TEST_CASE("compute_regression_pct basic cases") {
    CHECK(compute_regression_pct(10.0, 12.0) == doctest::Approx(20.0));
    CHECK(compute_regression_pct(10.0, 8.0) == doctest::Approx(-20.0));
    CHECK(compute_regression_pct(10.0, 10.0) == doctest::Approx(0.0));
    CHECK(compute_regression_pct(0.0, 5.0) == doctest::Approx(0.0));
}

TEST_CASE("benchmark_report_from_json handles missing optional fields") {
    nlohmann::json js;
    js["schema"] = "chronon3d.bench.v2";
    js["comp_id"] = "Minimal";

    auto loaded = benchmark_report_from_json(js);
    CHECK(loaded.comp_id == "Minimal");
    CHECK(loaded.width == 0);
    CHECK(loaded.metrics.avg_frame_ms == doctest::Approx(0.0));
    CHECK(loaded.memory.peak_rss_mb == doctest::Approx(0.0));
    CHECK(loaded.memory.peak_framebuffer_bytes == doctest::Approx(0.0));
    CHECK(loaded.memory.allocations_per_frame == doctest::Approx(0.0));
    CHECK(loaded.memory.bytes_copied_per_frame == doctest::Approx(0.0));
    CHECK(loaded.quality.deterministic_hash.empty());
    CHECK(loaded.quality.ssim == doctest::Approx(0.0));
    CHECK(loaded.counters.cache_hits == 0);
    CHECK(loaded.counters.framebuffer_copies == 0);
    CHECK(loaded.counters.framebuffer_clears == 0);
    CHECK(loaded.counters.full_frame_passes == 0);
    CHECK(loaded.categories_ms.empty());
    CHECK(loaded.frame_times_ms.empty());
}
