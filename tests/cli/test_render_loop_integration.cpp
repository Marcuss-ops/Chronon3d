#include <doctest/doctest.h>

#include <apps/chronon3d_cli/commands/video/common/pipe_export_session.hpp>
#include <apps/chronon3d_cli/commands/video/common/pipe_export_helpers.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <tests/helpers/test_utils.hpp>
#include <apps/chronon3d_cli/utils/job/cli_render_utils.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/math/color.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>
using namespace chronon3d;

using namespace chronon3d::cli;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Create a tiny, fast-to-render composition for integration tests.
static Composition make_integration_comp(int width, int height, int duration) {
    CompositionSpec spec;
    spec.name = "RenderLoopIntegration";
    spec.width = width;
    spec.height = height;
    spec.duration = static_cast<Frame>(duration);
    return Composition(spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.rect("bg", {
            .size = {static_cast<float>(ctx.width), static_cast<float>(ctx.height)},
            .color = Color{0.2f, 0.3f, 0.8f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f}
        });
        s.rect("box", {
            .size = {20.0f, 20.0f},
            .color = Color::red(),
            .pos = {5.0f, 5.0f, 0.0f}
        });
        return s.build();
    });
}

/// Set up a renderer suitable for render loop integration tests.
/// Uses the canonical create_renderer() factory instead of the
/// deprecated SoftwareRenderer(Config{}) path.
static std::shared_ptr<SoftwareRenderer> make_integration_renderer() {
    CompositionRegistry registry;
    RenderSettings settings;
    settings.use_modular_graph = true;
    return chronon3d::cli::create_renderer(registry, settings);
}

/// Background consumer that drains the queue and releases arena buffers.
/// Prevents the render loop from blocking on TripleBufferArena::acquire().
///
/// P1-19 — switched from busy-yield (try_dequeue + std::this_thread::yield)
/// to blocking `pop` + `queue.close()` pattern.  The test calls
/// `queue.close()` after `run_render_loop` returns; the consumer drains
/// any remaining items and exits when `pop` returns false (queue
/// closed + empty).
static void drain_queue_consumer(
    RenderFrameQueue<RenderFramePackage>& queue,
    TripleBufferArena& arena,
    std::atomic<int>& consumed_count)
{
    for (;;) {
        RenderFramePackage pkg;
        if (!queue.pop(pkg)) {
            return;  // queue closed and empty
        }
        arena.release(pkg.arena);
        ++consumed_count;
    }
}

/// Build a RenderLoopContext from the given components.
static RenderLoopContext make_loop_context(
    SoftwareRenderer& renderer,
    cache::NodeCache& node_cache,
    const CompositionRegistry& registry,
    const Composition& comp,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    RenderFrameQueue<RenderFramePackage>& queue,
    std::atomic<bool>& writer_failed,
    TripleBufferArena& triple_arena,
    std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames)
{
    return RenderLoopContext{
        .backend = renderer.backend(),
        .node_cache = node_cache,
        .settings = renderer.render_settings(),
        .registry = registry,
        .video_decoder = nullptr,
        .comp = comp,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = renderer,  // P1-20 — bind reference (renderer is SoftwareRenderer&).
        .queue = queue,
        .writer_failed = writer_failed,
        .triple_arena = triple_arena,
        .counters = renderer.counters(),
        .telemetry_frames = telemetry_frames,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — normal multi-frame success
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: multi-frame render produces all frames") {
    constexpr int W = 32, H = 32, FRAMES = 5;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, FRAMES);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<int> consumed{0};
    TripleBufferArena triple_arena(4, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    // Background consumer prevents arena acquire deadlock
    std::thread consumer(drain_queue_consumer,
        std::ref(queue), std::ref(triple_arena), std::ref(consumed));

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, FRAMES, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    queue.close();
    consumer.join();

    CHECK(result.status.success);
    CHECK_FALSE(result.status.cancelled);
    CHECK_FALSE(result.status.render_failed);
    CHECK_FALSE(result.status.writer_error);
    CHECK_FALSE(result.status.exception_error);
    CHECK(result.status.frames_enqueued == FRAMES);
    CHECK(result.status.frames_rendered == FRAMES);
    CHECK(telemetry_frames.size() == static_cast<size_t>(FRAMES));
    CHECK(result.render_graph_eval_ms > 0.0);
    CHECK(consumed.load() == FRAMES);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — single frame
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: single frame renders correctly") {
    constexpr int W = 32, H = 32;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, 1);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    // 4 arenas for 1 frame — no consumer needed
    TripleBufferArena triple_arena(4, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, 1, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    CHECK(result.status.success);
    CHECK(result.status.frames_enqueued == 1);
    CHECK(result.status.frames_rendered == 1);
    CHECK(telemetry_frames.size() == 1);
    CHECK(telemetry_frames[0].frame_number == 0);

    // P1-19 — close + pop pattern.  First pop returns the enqueued item
    // (queue is non-empty at this point), second pop returns false because
    // the queue is closed AND empty.
    queue.close();
    RenderFramePackage pkg;
    REQUIRE(queue.pop(pkg));
    CHECK(pkg.framebuffer != nullptr);
    CHECK(pkg.framebuffer->width() == W);
    CHECK(pkg.framebuffer->height() == H);
    triple_arena.release(pkg.arena);
    CHECK_FALSE(queue.pop(pkg));
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — cancellation stops rendering
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: cancellation stops render loop early") {
    constexpr int W = 32, H = 32, FRAMES = 10;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, FRAMES);

    CancellationToken token;
    FfmpegExportOptions opts;
    opts.cancellation_token = &token;
    // Pre-cancel: the loop checks the token before rendering and exits immediately.
    token.cancel();

    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    TripleBufferArena triple_arena(2, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, FRAMES, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    CHECK_FALSE(result.status.success);
    CHECK(result.status.cancelled);
    CHECK(result.status.frames_enqueued == 0);
    CHECK(result.status.frames_rendered == 0);
    CHECK(telemetry_frames.empty());

    // P1-19 — close + pop pattern; queue is empty after cancellation
    // (render loop checked the token and broke before any push).
    queue.close();
    RenderFramePackage pkg;
    CHECK_FALSE(queue.pop(pkg));
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — writer failure stops loop
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: pre-set writer failure stops loop") {
    constexpr int W = 32, H = 32, FRAMES = 5;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, FRAMES);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{true}; // Pre-set failure
    TripleBufferArena triple_arena(2, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, FRAMES, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    CHECK_FALSE(result.status.success);
    CHECK(result.status.writer_error);
    CHECK_FALSE(result.status.cancelled);
    CHECK_FALSE(result.status.render_failed);
    CHECK(result.status.frames_enqueued == 0);
    CHECK(result.status.frames_rendered == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — telemetry frame ordering
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: telemetry frames are in display order") {
    constexpr int W = 32, H = 32, FRAMES = 8;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, FRAMES);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<int> consumed{0};
    TripleBufferArena triple_arena(4, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    // 8 frames with 4 arenas — need a consumer to avoid deadlock
    std::thread consumer(drain_queue_consumer,
        std::ref(queue), std::ref(triple_arena), std::ref(consumed));

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, FRAMES, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    queue.close();
    consumer.join();

    CHECK(result.status.success);
    CHECK(telemetry_frames.size() == static_cast<size_t>(FRAMES));

    for (int i = 0; i < FRAMES; ++i) {
        CHECK(telemetry_frames[i].frame_number == i);
        CHECK(telemetry_frames[i].duration_ms > 0.0);
    }
    CHECK(consumed.load() == FRAMES);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — partial frame range
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: partial frame range [3, 7)") {
    constexpr int W = 32, H = 32;
    constexpr Frame START = 3, END = 7;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, 10);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<int> consumed{0};
    // 4 arenas for 4 frames — just at the limit, use consumer to be safe
    TripleBufferArena triple_arena(4, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    std::thread consumer(drain_queue_consumer,
        std::ref(queue), std::ref(triple_arena), std::ref(consumed));

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        START, END, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    queue.close();
    consumer.join();

    CHECK(result.status.success);
    CHECK(result.status.frames_enqueued == static_cast<int>(END - START));
    CHECK(result.status.frames_rendered == static_cast<int>(END - START));
    CHECK(telemetry_frames.size() == static_cast<size_t>(END - START));

    for (int i = 0; i < static_cast<int>(END - START); ++i) {
        CHECK(telemetry_frames[i].frame_number == static_cast<int>(START + i));
    }
    CHECK(consumed.load() == static_cast<int>(END - START));
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — writer failure detected during render
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: writer failure during render stops loop") {
    constexpr int W = 32, H = 32, FRAMES = 20;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, FRAMES);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<int> consumed{0};
    TripleBufferArena triple_arena(4, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    // P1-19 — switched from busy-yield (try_dequeue + yield) to blocking
    // pop + queue.close() pattern.  Consumer pops items, sets writer_failed
    // after 3 items, then continues popping (to release arenas for the
    // render loop, since push is non-blocking on an unbounded queue) until
    // the test calls queue.close() to signal end-of-stream.
    std::thread consumer([&]() {
        int count = 0;
        for (;;) {
            RenderFramePackage pkg;
            if (!queue.pop(pkg)) {
                consumed.store(count);
                return;  // queue closed and empty
            }
            triple_arena.release(pkg.arena);
            ++count;
            if (count >= 3) {
                writer_failed.store(true, std::memory_order_release);
                // Continue popping — render loop's push is non-blocking
                // (unbounded queue), but the consumer must release arenas
                // so the render loop can continue acquiring.
            }
        }
    });

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        0, FRAMES, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    queue.close();
    consumer.join();

    CHECK_FALSE(result.status.success);
    CHECK(result.status.writer_error);
    CHECK(result.status.frames_enqueued > 0);
    CHECK(result.status.frames_enqueued <= FRAMES);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration: Render loop — empty range (start == end)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("RenderLoop Integration: empty frame range produces no frames") {
    constexpr int W = 32, H = 32;

    auto renderer = make_integration_renderer();
    cache::NodeCache node_cache;
    CompositionRegistry registry;
    Composition comp = make_integration_comp(W, H, 5);

    FfmpegExportOptions opts;
    RenderFrameQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    TripleBufferArena triple_arena(2, static_cast<size_t>(W) * H * sizeof(Color) * 8);

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    auto loop_ctx = make_loop_context(
        *renderer, node_cache, registry, comp,
        5, 5, opts, queue, writer_failed, triple_arena, telemetry_frames);

    auto result = run_render_loop(loop_ctx);

    CHECK(result.status.success);
    CHECK(result.status.frames_enqueued == 0);
    CHECK(result.status.frames_rendered == 0);
    CHECK(telemetry_frames.empty());

    // P1-19 — close + pop pattern; queue is empty after empty range
    // (start == end, render loop body never entered).
    queue.close();
    RenderFramePackage pkg;
    CHECK_FALSE(queue.pop(pkg));
}
