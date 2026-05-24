#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <set>

using namespace chronon3d::telemetry;

// Collect all field names present in a JSON object (top-level only)
static std::set<std::string> json_field_names(const nlohmann::json& j) {
    std::set<std::string> names;
    for (auto it = j.begin(); it != j.end(); ++it) {
        names.insert(it.key());
    }
    return names;
}

TEST_CASE("Telemetry Coherence: JSONL serializes all RenderTelemetryRecord timing fields") {
    auto temp_dir = std::filesystem::temp_directory_path() / "chronon3d_test_coherence";
    std::filesystem::create_directories(temp_dir);
    auto jsonl_path = (temp_dir / "test_coherence.jsonl").string();
    std::filesystem::remove(jsonl_path);

    auto store = std::make_shared<JsonlTelemetryStore>();
    REQUIRE(store->initialize(jsonl_path));

    RenderTelemetryRecord run;
    run.run_id = "coh_test";
    run.composition_id = "coh_comp";

    // Set all timing/counter fields to distinct known values
    run.framebuffer_acquire_ms = 10;
    run.framebuffer_clear_ms = 20;
    run.clearnode_ms = 30;
    run.framebuffer_pool_clear_ms = 40;
    run.framebuffer_enqueue_ms = 50;
    run.framebuffer_pool_miss_count_size_mismatch = 3;
    run.framebuffer_pool_miss_count_empty = 4;
    run.framebuffer_pool_hits = 100;
    run.framebuffer_buffer_returned_to_pool_count = 90;
    run.unaligned_memory_copies = 5;
    run.frame_conversion_copy_ms = 60;
    run.video_graph_eval_ms = 70;
    run.video_conversion_ms = 80;
    run.video_pipe_write_ms = 90;
    run.video_ffmpeg_latency_ms = 100;
    run.io_queue_push_blocked_ms = 110;
    run.io_queue_pop_wait_ms = 120;
    run.io_queue_peak_depth = 16;
    run.ffmpeg_pipe_write_blocked_ms = 130;
    run.converted_frame_cache_hits = 131;
    run.ffmpeg_flush_ms = 140;

    // Also set basic counters
    run.clear_calls = 200;
    run.clear_pixels = 1000000;
    run.clear_skipped_calls = 50;
    run.clear_skipped_pixels = 500000;
    run.pixels_touched = 2000000;
    run.cache_hits = 80;
    run.cache_misses = 20;
    run.nodes_executed = 42;
    run.dirty_full_fallbacks = 3;

    REQUIRE(store->write_render_run(run));

    // Read back and verify
    std::ifstream file(jsonl_path);
    REQUIRE(file.is_open());

    std::string line;
    REQUIRE(std::getline(file, line));
    auto j = nlohmann::json::parse(line);

    CHECK(j["type"] == "run");
    CHECK(j["run_id"] == "coh_test");

    // All pipeline timing fields must be present with correct values
    CHECK(j["framebuffer_acquire_ms"] == 10);
    CHECK(j["framebuffer_clear_ms"] == 20);
    CHECK(j["clearnode_ms"] == 30);
    CHECK(j["framebuffer_pool_clear_ms"] == 40);
    CHECK(j["framebuffer_enqueue_ms"] == 50);
    CHECK(j["framebuffer_pool_hits"] == 100);
    CHECK(j["framebuffer_buffer_returned_to_pool_count"] == 90);
    CHECK(j["unaligned_memory_copies"] == 5);
    CHECK(j["frame_conversion_copy_ms"] == 60);
    CHECK(j["video_graph_eval_ms"] == 70);
    CHECK(j["video_conversion_ms"] == 80);
    CHECK(j["video_pipe_write_ms"] == 90);
    CHECK(j["video_ffmpeg_latency_ms"] == 100);
    CHECK(j["io_queue_push_blocked_ms"] == 110);
    CHECK(j["io_queue_pop_wait_ms"] == 120);
    CHECK(j["io_queue_peak_depth"] == 16);
    CHECK(j["ffmpeg_pipe_write_blocked_ms"] == 130);
    CHECK(j["converted_frame_cache_hits"] == 131);
    CHECK(j["ffmpeg_flush_ms"] == 140);

    // Clear counters must be present with correct values
    CHECK(j["clear_calls"] == 200);
    CHECK(j["clear_pixels"] == 1000000);
    CHECK(j["clear_skipped_calls"] == 50);
    CHECK(j["clear_skipped_pixels"] == 500000);

    // Basic counters
    CHECK(j["pixels_touched"] == 2000000);
    CHECK(j["cache_hits"] == 80);
    CHECK(j["dirty_full_fallbacks"] == 3);

    file.close();
    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("Telemetry Coherence: JSONL field names match semantic categories") {
    auto temp_dir = std::filesystem::temp_directory_path() / "chronon3d_test_coherence2";
    std::filesystem::create_directories(temp_dir);
    auto jsonl_path = (temp_dir / "test_fields.jsonl").string();
    std::filesystem::remove(jsonl_path);

    auto store = std::make_shared<JsonlTelemetryStore>();
    REQUIRE(store->initialize(jsonl_path));

    RenderTelemetryRecord run;
    run.run_id = "fields_test";

    // Set non-zero so fields are serialized
    run.framebuffer_acquire_ms = 1;
    run.framebuffer_clear_ms = 1;
    run.clearnode_ms = 1;
    run.framebuffer_pool_clear_ms = 1;
    run.framebuffer_enqueue_ms = 1;
    run.framebuffer_pool_hits = 1;
    run.frame_conversion_copy_ms = 1;
    run.video_conversion_ms = 1;
    run.video_pipe_write_ms = 1;
    run.unaligned_memory_copies = 1;
    run.io_queue_push_blocked_ms = 1;
    run.io_queue_pop_wait_ms = 1;
    run.io_queue_peak_depth = 1;
    run.ffmpeg_pipe_write_blocked_ms = 1;
    run.converted_frame_cache_hits = 1;
    run.ffmpeg_flush_ms = 1;
    run.clear_skipped_calls = 1;
    run.clear_skipped_pixels = 1;
    run.clear_calls = 1;

    REQUIRE(store->write_render_run(run));

    std::ifstream file(jsonl_path);
    REQUIRE(file.is_open());
    std::string line;
    REQUIRE(std::getline(file, line));
    auto j = nlohmann::json::parse(line);

    auto fields = json_field_names(j);

    // Verify fields from each semantic group are present
    // Render group
    CHECK(fields.count("clearnode_ms") == 1);

    // Framebuffer group
    CHECK(fields.count("framebuffer_acquire_ms") == 1);
    CHECK(fields.count("framebuffer_clear_ms") == 1);
    CHECK(fields.count("framebuffer_pool_clear_ms") == 1);
    CHECK(fields.count("framebuffer_enqueue_ms") == 1);
    CHECK(fields.count("framebuffer_pool_hits") == 1);

    // Clear group
    CHECK(fields.count("clear_calls") == 1);
    CHECK(fields.count("clear_skipped_calls") == 1);
    CHECK(fields.count("clear_skipped_pixels") == 1);

    // Conversion group
    CHECK(fields.count("frame_conversion_copy_ms") == 1);
    CHECK(fields.count("video_conversion_ms") == 1);
    CHECK(fields.count("video_pipe_write_ms") == 1);
    CHECK(fields.count("unaligned_memory_copies") == 1);

    // Queue group
    CHECK(fields.count("io_queue_push_blocked_ms") == 1);
    CHECK(fields.count("io_queue_pop_wait_ms") == 1);
    CHECK(fields.count("io_queue_peak_depth") == 1);

    // FFmpeg pipe group
    CHECK(fields.count("ffmpeg_pipe_write_blocked_ms") == 1);
    CHECK(fields.count("converted_frame_cache_hits") == 1);
    CHECK(fields.count("ffmpeg_flush_ms") == 1);

    file.close();
    std::filesystem::remove_all(temp_dir);
}
