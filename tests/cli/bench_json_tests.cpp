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
    report.metrics.avg_frame_ms = 14.5;
    report.metrics.median_frame_ms = 13.8;
    report.metrics.min_frame_ms = 12.0;
    report.metrics.max_frame_ms = 25.0;
    report.metrics.p95_frame_ms = 18.2;
    report.metrics.fps = 68.9;
    report.counters.cache_hits = 1000;
    report.counters.cache_misses = 50;
    report.counters.cache_hit_rate = 0.95;
    report.counters.nodes_executed = 3000;
    report.counters.pixels_touched = 1000000;
    report.counters.blur_pixels = 200000;
    report.counters.images_sampled = 500000;
    report.counters.text_glyphs_rasterized = 100;
    report.categories_ms["graph"] = 120.0;
    report.categories_ms["raster"] = 900.0;

    auto js = to_json(report, false);

    CHECK(js["schema"] == "chronon3d.bench.v2");
    CHECK(js["comp_id"] == "TestComp");
    CHECK(js["render"]["width"] == 1920);
    CHECK(js["render"]["height"] == 1080);
    CHECK(js["render"]["frames"] == 120);
    CHECK(js["render"]["warmup"] == 10);
    CHECK(js["render"]["modular_graph"] == true);

    CHECK(js["metrics"]["avg_frame_ms"] == doctest::Approx(14.5));
    CHECK(js["metrics"]["median_frame_ms"] == doctest::Approx(13.8));
    CHECK(js["metrics"]["min_frame_ms"] == doctest::Approx(12.0));
    CHECK(js["metrics"]["max_frame_ms"] == doctest::Approx(25.0));
    CHECK(js["metrics"]["p95_frame_ms"] == doctest::Approx(18.2));
    CHECK(js["metrics"]["fps"] == doctest::Approx(68.9));

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
    original.metrics.avg_frame_ms = 20.0;
    original.metrics.p95_frame_ms = 30.0;
    original.counters.cache_hits = 500;
    original.counters.cache_misses = 25;
    original.categories_ms["composite"] = 300.0;
    original.frame_times_ms = {15.0, 16.0, 17.0};

    auto js = to_json(original, true);
    auto loaded = benchmark_report_from_json(js);

    CHECK(loaded.schema == "chronon3d.bench.v2");
    CHECK(loaded.comp_id == "RoundtripComp");
    CHECK(loaded.width == 1280);
    CHECK(loaded.height == 720);
    CHECK(loaded.frames == 60);
    CHECK(loaded.warmup == 5);
    CHECK(loaded.modular_graph == false);
    CHECK(loaded.metrics.avg_frame_ms == doctest::Approx(20.0));
    CHECK(loaded.metrics.p95_frame_ms == doctest::Approx(30.0));
    CHECK(loaded.counters.cache_hits == 500);
    CHECK(loaded.counters.cache_misses == 25);
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
    CHECK(loaded.counters.cache_hits == 0);
    CHECK(loaded.categories_ms.empty());
    CHECK(loaded.frame_times_ms.empty());
}
