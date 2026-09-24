#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace chronon3d::graph {

TEST_CASE("ScopedNodeMemory tracks live and peak bytes without implicit allocation events") {
    NodeMemoryTracker tracker;
    const TemporalSampleKey sample{Frame{3}, 17, 1};

    {
        ScopedNodeMemory outer(tracker, "blur", sample, 1024);
        outer.record_allocation(2048);
        {
            ScopedNodeMemory inner(tracker, "blur", sample, 4096);
            CHECK(tracker.snapshot().current_live_bytes == 5120);
            CHECK(tracker.snapshot().peak_live_bytes == 5120);
        }
        const auto live_report = tracker.snapshot();
        CHECK(live_report.current_live_bytes == 1024);
        REQUIRE(live_report.nodes.size() == 1);
        CHECK(live_report.nodes.front().live_bytes == 1024);
        CHECK(live_report.nodes.front().peak_live_bytes == 5120);
    }

    const auto report = tracker.snapshot();
    CHECK(report.current_live_bytes == 0);
    CHECK(report.peak_live_bytes == 5120);
    REQUIRE(report.nodes.size() == 1);
    CHECK(report.nodes.front().node_id == "blur");
    CHECK(report.nodes.front().allocations == 1);
    CHECK(report.nodes.front().allocated_bytes == 2048);
    CHECK(report.nodes.front().temporary_buffers == 2);
    CHECK(report.nodes.front().live_bytes == 0);
    CHECK(report.nodes.front().peak_live_bytes == 5120);
    REQUIRE(report.samples.size() == 1);
    CHECK(report.samples.front().sample_key == sample);
    CHECK(report.samples.front().temporary_bytes_observed == 1024 + 4096);
    CHECK(report.samples.front().live_bytes == 0);
    CHECK(report.samples.front().peak_live_bytes == 5120);
}

TEST_CASE("ScopedNodeMemory attributes framebuffer traffic to the active node") {
    NodeMemoryTracker tracker;
    std::uint64_t expected_framebuffer_bytes = 0;
    {
        ScopedNodeMemory node_scope(tracker, "copy-node", std::nullopt, 64, "Copy");
        node_scope.record_read(4, 16);
        node_scope.record_write(4, 16);

        chronon3d::Framebuffer source(4, 4);
        expected_framebuffer_bytes += source.size_bytes();
        source.clear(chronon3d::Color::transparent());
        chronon3d::Framebuffer copied(source);
        expected_framebuffer_bytes += copied.size_bytes();
        copied.clear(chronon3d::Color::transparent());
    }

    const auto report = tracker.snapshot();
    REQUIRE(report.nodes.size() == 1);
    const auto& node = report.nodes.front();
    CHECK(node.pixels_read == 4);
    CHECK(node.pixels_written == 4);
    CHECK(node.bytes_read == 16);
    CHECK(node.bytes_written == 16);
    CHECK(node.framebuffer_copies == 1);
    CHECK(node.framebuffer_clears == 2);
    CHECK(node.allocations == 2);
    CHECK(node.allocated_bytes == expected_framebuffer_bytes);
    CHECK(node.temporary_buffers == 1);
}

TEST_CASE("NodeMemoryTracker isolates temporal sample domains") {
    NodeMemoryTracker tracker;
    const TemporalSampleKey first{Frame{0}, 10, 1};
    const TemporalSampleKey second{Frame{0}, 20, 1};

    {
        ScopedNodeMemory a(tracker, "node", first, 100);
        ScopedNodeMemory b(tracker, "node", second, 300);
        a.record_allocation(64);
        const auto report = tracker.snapshot();
        REQUIRE(report.samples.size() == 2);
        CHECK(report.samples[0].sample_key == first);
        CHECK(report.samples[1].sample_key == second);
        CHECK(report.samples[0].live_bytes == 100);
        CHECK(report.samples[1].live_bytes == 300);
        CHECK(report.samples[0].peak_live_bytes == 100);
        CHECK(report.samples[1].peak_live_bytes == 300);
        REQUIRE(report.nodes.size() == 1);
        CHECK(report.nodes.front().live_bytes == 400);
        CHECK(report.nodes.front().peak_live_bytes == 400);
        CHECK(report.nodes.front().allocations == 1);
        CHECK(report.nodes.front().allocated_bytes == 64);
    }        CHECK(tracker.snapshot().current_live_bytes == 0);
}

TEST_CASE("NodeMemoryTracker supports concurrent observations without lost counters") {
    NodeMemoryTracker tracker;
    constexpr int thread_count = 4;
    constexpr int observations_per_thread = 100;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (int t = 0; t < thread_count; ++t) {
        workers.emplace_back([&tracker] {
            for (int i = 0; i < observations_per_thread; ++i) {
                NodeMemoryMetrics metrics;
                metrics.pixels_read.store(1, std::memory_order_relaxed);
                tracker.observe_node("parallel", metrics);
            }
        });
    }
    for (auto& worker : workers) worker.join();

    const auto report = tracker.snapshot();
    REQUIRE(report.nodes.size() == 1);
    CHECK(report.nodes.front().pixels_read == thread_count * observations_per_thread);
}

TEST_CASE("NodeMemoryTracker reset clears node, sample, pool, and peak domains") {
    NodeMemoryTracker tracker;
    tracker.record_pool(NodeMemoryPoolSnapshot{
        .current_bytes = 10,
        .retained_bytes = 20,
        .peak_retained_bytes = 30});
    tracker.record_rss_peak(1234);
    {
        ScopedNodeMemory scope(tracker, "node", TemporalSampleKey{}, 4096);
        scope.record_allocation(512);
    }
    const auto allocation_report = tracker.snapshot();
    REQUIRE(allocation_report.nodes.size() == 1);
    CHECK(allocation_report.nodes.front().allocations == 1);
    CHECK(allocation_report.nodes.front().allocated_bytes == 512);
    CHECK(tracker.snapshot().peak_live_bytes == 4096);

    tracker.reset();
    const auto report = tracker.snapshot();
    CHECK(report.nodes.empty());
    CHECK(report.samples.empty());
    CHECK(report.current_live_bytes == 0);
    CHECK(report.peak_live_bytes == 0);
    CHECK(report.peak_rss_bytes == 0);
    CHECK(report.framebuffer_pool.current_bytes == 0);
    CHECK(report.framebuffer_pool.peak_retained_bytes == 0);
}

TEST_CASE("ScopedNodeMemory attributes framebuffer operations and pixel volumes") {
    NodeMemoryTracker tracker;
    {
        ScopedNodeMemory scope(tracker, "composite", std::nullopt, 0, "Composite");
        scope.record_read(16, 256);
        scope.record_write(16, 256);

        Framebuffer source(4, 4);
        Framebuffer copied(source);
        copied.clear(Color::transparent());
        framebuffer_clear_parallel(copied, Color::transparent(), std::nullopt);
    }

    const auto report = tracker.snapshot();
    REQUIRE(report.nodes.size() == 1);
    const auto& node = report.nodes.front();
    CHECK(node.node_id == "composite");
    CHECK(node.node_type == "Composite");
    CHECK(node.pixels_read == 16);
    CHECK(node.bytes_read == 256);
    CHECK(node.pixels_written == 16);
    CHECK(node.bytes_written == 256);
    CHECK(node.allocations >= 2);
    CHECK(node.allocated_bytes >= 2 * 4 * 4 * sizeof(Color));
    CHECK(node.framebuffer_copies == 1);
    CHECK(node.framebuffer_clears == 2);
}

} // namespace chronon3d::graph
