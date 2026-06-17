#include <doctest/doctest.h>
#include <chronon3d/core/profiling/counters.hpp>
#include <vector>
#include <algorithm>
using namespace chronon3d;


// Helper: reset all counters to zero
static void reset_counters(RenderCounters& c) {
#define X(name) c.name.store(0, std::memory_order_relaxed);
    CHRONON_RENDER_COUNTERS(X)
#undef X
}

// Helper: set all counters to a known unique value (1-based index)
static void set_known_values(RenderCounters& c) {
    int idx = 1;
#define X(name) c.name.store(static_cast<uint64_t>(idx++), std::memory_order_relaxed);
    CHRONON_RENDER_COUNTERS(X)
#undef X
}

TEST_CASE("Telemetry Semantic: Render counters do not overlap with Queue counters") {
    RenderCounters c;
    reset_counters(c);

    // Render-stage counters
    c.clearnode_ms.store(100, std::memory_order_relaxed);
    c.video_graph_eval_ms.store(50, std::memory_order_relaxed);
    c.nodes_executed.store(42, std::memory_order_relaxed);

    // Queue-stage counters (separate stage)
    c.io_queue_push_blocked_ms.store(200, std::memory_order_relaxed);
    c.io_queue_pop_wait_ms.store(150, std::memory_order_relaxed);
    c.io_queue_peak_depth.store(8, std::memory_order_relaxed);

    // Verify render counters are independent of queue counters
    CHECK(c.clearnode_ms.load() == 100);
    CHECK(c.video_graph_eval_ms.load() == 50);
    CHECK(c.nodes_executed.load() == 42);
    CHECK(c.io_queue_push_blocked_ms.load() == 200);
    CHECK(c.io_queue_pop_wait_ms.load() == 150);
    CHECK(c.io_queue_peak_depth.load() == 8);

    // No queue field should leak into render fields
    CHECK(c.clearnode_ms.load() != c.io_queue_push_blocked_ms.load());
    CHECK(c.video_graph_eval_ms.load() != c.io_queue_pop_wait_ms.load());
}

TEST_CASE("Telemetry Semantic: Framebuffer acquire is not confused with framebuffer clear") {
    RenderCounters c;
    reset_counters(c);

    c.framebuffer_acquire_ms.store(30, std::memory_order_relaxed);
    c.framebuffer_clear_ms.store(80, std::memory_order_relaxed);
    c.framebuffer_pool_clear_ms.store(15, std::memory_order_relaxed);

    // Each must retain its own value
    CHECK(c.framebuffer_acquire_ms.load() == 30);
    CHECK(c.framebuffer_clear_ms.load() == 80);
    CHECK(c.framebuffer_pool_clear_ms.load() == 15);

    // Acquire != clear (semantically distinct)
    CHECK(c.framebuffer_acquire_ms.load() != c.framebuffer_clear_ms.load());
    CHECK(c.framebuffer_clear_ms.load() != c.framebuffer_pool_clear_ms.load());
}

TEST_CASE("Telemetry Semantic: frame_conversion_copy_ms does not include pipe write") {
    RenderCounters c;
    reset_counters(c);

    c.frame_conversion_copy_ms.store(45, std::memory_order_relaxed);
    c.video_conversion_ms.store(45, std::memory_order_relaxed);
    c.video_pipe_write_ms.store(120, std::memory_order_relaxed);

    // Conversion is separate from pipe write
    CHECK(c.frame_conversion_copy_ms.load() == 45);
    CHECK(c.video_pipe_write_ms.load() == 120);

    // They are distinct counters, not aliases
    CHECK(&c.frame_conversion_copy_ms != &c.video_pipe_write_ms);
    CHECK(&c.frame_conversion_copy_ms != &c.ffmpeg_pipe_write_blocked_ms);
}

TEST_CASE("Telemetry Semantic: clear_skipped is a counter separate from clear timing") {
    RenderCounters c;
    reset_counters(c);

    c.clear_skipped_calls.store(500, std::memory_order_relaxed);
    c.clear_skipped_pixels.store(1000000, std::memory_order_relaxed);
    c.clearnode_ms.store(25, std::memory_order_relaxed);
    c.clear_calls.store(200, std::memory_order_relaxed);

    // Skipped calls/pixels are counters, not timing
    CHECK(c.clear_skipped_calls.load() == 500);
    CHECK(c.clear_skipped_pixels.load() == 1000000);
    CHECK(c.clearnode_ms.load() == 25);
    CHECK(c.clear_calls.load() == 200);

    // The skipped counters are distinct from the clear timing counter
    CHECK(&c.clear_skipped_calls != &c.clearnode_ms);
    CHECK(&c.clear_skipped_pixels != &c.clearnode_ms);
}

TEST_CASE("Telemetry Semantic: ffmpeg_flush_ms is separate from ffmpeg_pipe_write_blocked_ms") {
    RenderCounters c;
    reset_counters(c);

    c.ffmpeg_pipe_write_blocked_ms.store(300, std::memory_order_relaxed);
    c.ffmpeg_flush_ms.store(50, std::memory_order_relaxed);

    CHECK(c.ffmpeg_pipe_write_blocked_ms.load() == 300);
    CHECK(c.ffmpeg_flush_ms.load() == 50);

    // Flush is a distinct stage from pipe write blocked
    CHECK(&c.ffmpeg_flush_ms != &c.ffmpeg_pipe_write_blocked_ms);
    CHECK(c.ffmpeg_flush_ms.load() != c.ffmpeg_pipe_write_blocked_ms.load());
}

TEST_CASE("Telemetry Semantic: All counters can hold known unique values independently") {
    RenderCounters c;
    reset_counters(c);
    set_known_values(c);

    // After setting all counters to unique 1-based indices,
    // verify that key stage-group pairs are independent.
    // We just verify a representative sample from each semantic group.

    // Render group
    CHECK(c.clearnode_ms.load() > 0);
    CHECK(c.nodes_executed.load() > 0);

    // Framebuffer group
    CHECK(c.framebuffer_acquire_ms.load() > 0);
    CHECK(c.framebuffer_clear_ms.load() > 0);

    // Clear group
    CHECK(c.clear_calls.load() > 0);
    CHECK(c.clear_skipped_calls.load() > 0);

    // Conversion group
    CHECK(c.frame_conversion_copy_ms.load() > 0);
    CHECK(c.unaligned_memory_copies.load() > 0);

    // Queue group
    CHECK(c.io_queue_push_blocked_ms.load() > 0);
    CHECK(c.io_queue_peak_depth.load() > 0);

    // FFmpeg pipe group
    CHECK(c.ffmpeg_pipe_write_blocked_ms.load() > 0);
    CHECK(c.ffmpeg_flush_ms.load() > 0);

    // Every counter must have a unique value (no aliasing)
    std::vector<uint64_t> values;
#define X(name) values.push_back(c.name.load(std::memory_order_relaxed));
    CHRONON_RENDER_COUNTERS(X)
#undef X

    std::sort(values.begin(), values.end());
    auto dup = std::adjacent_find(values.begin(), values.end());
    CHECK(dup == values.end());  // No duplicate values → no counter aliasing
}
