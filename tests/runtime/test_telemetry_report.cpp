#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>

#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using namespace chronon3d;
using namespace chronon3d::telemetry;

// ── Helper: collect all counter names from the X-macro ────────────────────
static std::vector<std::string> all_counter_names() {
    std::vector<std::string> names;
#define X(name) names.push_back(#name);
    CHRONON_RENDER_COUNTERS(X)
#undef X
    return names;
}

// ── Section grouping definitions ──────────────────────────────────────────
// Each section maps to fields needed in the markdown report.
// These must stay in sync with command_telemetry_report.cpp

static const std::vector<std::string> RENDER_FIELDS = {
    "clearnode_ms", "video_graph_eval_ms"
};

static const std::vector<std::string> FRAMEBUFFER_FIELDS = {
    "framebuffer_acquire_ms", "framebuffer_clear_ms", "framebuffer_pool_clear_ms",
    "framebuffer_enqueue_ms", "framebuffer_pool_hits",
    "framebuffer_pool_miss_count_size_mismatch", "framebuffer_pool_miss_count_empty",
    "framebuffer_buffer_returned_to_pool_count"
};

static const std::vector<std::string> CLEAR_FIELDS = {
    "clear_calls", "clear_pixels", "clear_skipped_calls", "clear_skipped_pixels"
};

static const std::vector<std::string> CONVERSION_FIELDS = {
    "frame_conversion_copy_ms", "video_conversion_ms",
    "video_pipe_write_ms", "unaligned_memory_copies"
};

static const std::vector<std::string> QUEUE_FIELDS = {
    "io_queue_push_blocked_ms", "io_queue_pop_wait_ms", "io_queue_peak_depth"
};

static const std::vector<std::string> FFMPEG_PIPE_FIELDS = {
    "ffmpeg_pipe_write_blocked_ms", "converted_frame_cache_hits", "ffmpeg_flush_ms", "video_ffmpeg_latency_ms"
};

TEST_CASE("Telemetry Report: All section fields exist in CHRONON_RENDER_COUNTERS") {
    auto names = all_counter_names();
    auto has = [&](const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };

    // Every field referenced in a report section must exist as a counter
    for (const auto& f : RENDER_FIELDS)          { CHECK(has(f)); }
    for (const auto& f : FRAMEBUFFER_FIELDS)      { CHECK(has(f)); }
    for (const auto& f : CLEAR_FIELDS)            { CHECK(has(f)); }
    for (const auto& f : CONVERSION_FIELDS)       { CHECK(has(f)); }
    for (const auto& f : QUEUE_FIELDS)            { CHECK(has(f)); }
    for (const auto& f : FFMPEG_PIPE_FIELDS)      { CHECK(has(f)); }
}

TEST_CASE("Telemetry Report: All section fields exist in RenderTelemetryRecord") {
    // Verify that every field used in the markdown report sections
    // is present in the RenderTelemetryRecord struct (compile-time check).
    RenderTelemetryRecord r{};

    // Render section
    r.clearnode_ms = 1;
    r.video_graph_eval_ms = 2;

    // Framebuffer section
    r.framebuffer_acquire_ms = 10;
    r.framebuffer_clear_ms = 11;
    r.framebuffer_pool_clear_ms = 12;
    r.framebuffer_enqueue_ms = 13;
    r.framebuffer_pool_hits = 14;
    r.framebuffer_pool_miss_count_size_mismatch = 15;
    r.framebuffer_pool_miss_count_empty = 16;
    r.framebuffer_buffer_returned_to_pool_count = 17;

    // Clear section
    r.clear_calls = 20;
    r.clear_pixels = 21;
    r.clear_skipped_calls = 22;
    r.clear_skipped_pixels = 23;

    // Conversion section
    r.frame_conversion_copy_ms = 30;
    r.video_conversion_ms = 31;
    r.video_pipe_write_ms = 32;
    r.unaligned_memory_copies = 33;

    // Queue section
    r.io_queue_push_blocked_ms = 40;
    r.io_queue_pop_wait_ms = 41;
    r.io_queue_peak_depth = 42;

    // FFmpeg pipe section
    r.ffmpeg_pipe_write_blocked_ms = 50;
    r.converted_frame_cache_hits = 7;
    r.ffmpeg_flush_ms = 51;
    r.video_ffmpeg_latency_ms = 52;

    // All values must stick (no zero-init fallback from missing fields)
    CHECK(r.clearnode_ms == 1);
    CHECK(r.video_graph_eval_ms == 2);
    CHECK(r.framebuffer_acquire_ms == 10);
    CHECK(r.framebuffer_clear_ms == 11);
    CHECK(r.framebuffer_pool_clear_ms == 12);
    CHECK(r.framebuffer_enqueue_ms == 13);
    CHECK(r.framebuffer_pool_hits == 14);
    CHECK(r.clear_calls == 20);
    CHECK(r.clear_pixels == 21);
    CHECK(r.clear_skipped_calls == 22);
    CHECK(r.clear_skipped_pixels == 23);
    CHECK(r.frame_conversion_copy_ms == 30);
    CHECK(r.video_conversion_ms == 31);
    CHECK(r.video_pipe_write_ms == 32);
    CHECK(r.unaligned_memory_copies == 33);
    CHECK(r.io_queue_push_blocked_ms == 40);
    CHECK(r.io_queue_pop_wait_ms == 41);
    CHECK(r.io_queue_peak_depth == 42);
    CHECK(r.ffmpeg_pipe_write_blocked_ms == 50);
    CHECK(r.converted_frame_cache_hits == 7);
    CHECK(r.ffmpeg_flush_ms == 51);
    CHECK(r.video_ffmpeg_latency_ms == 52);
}

TEST_CASE("Telemetry Report: Sections are mutually exclusive (no field in >1 section)") {
    // Collect all section fields into one vector and check for duplicates
    std::vector<std::string> all;
    all.insert(all.end(), RENDER_FIELDS.begin(), RENDER_FIELDS.end());
    all.insert(all.end(), FRAMEBUFFER_FIELDS.begin(), FRAMEBUFFER_FIELDS.end());
    all.insert(all.end(), CLEAR_FIELDS.begin(), CLEAR_FIELDS.end());
    all.insert(all.end(), CONVERSION_FIELDS.begin(), CONVERSION_FIELDS.end());
    all.insert(all.end(), QUEUE_FIELDS.begin(), QUEUE_FIELDS.end());
    all.insert(all.end(), FFMPEG_PIPE_FIELDS.begin(), FFMPEG_PIPE_FIELDS.end());

    std::sort(all.begin(), all.end());
    auto dup = std::adjacent_find(all.begin(), all.end());
    CHECK(dup == all.end());  // No field appears in more than one section
}


