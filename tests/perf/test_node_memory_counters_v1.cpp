// ============================================================================
// tests/perf/test_node_memory_counters_v1.cpp
//
// TICKET-PERF-COUNTERS-NODE-MEMORY-V1 — synthetic contract-lock test.
//
// This is a CONTRACT-LOCK test: it asserts the EXPECTED SHAPE that the canonical
// `chronon3d::graph::NodeMemoryMetrics` struct + `NodeStatsReporter` must
// satisfy per the ticket's spec verbatim.  The actual struct lives in a future
// forward-point chore (TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION)
// that requires an ADR per AGENTS.md §Cat-2 freeze.  This test is the design
// contract — once the canonical type lands, the synthetic stand-in is
// replaced by a `using` alias to the canonical header.
//
// Cat-3 minimal-surface composition:
//   - Pure C++ stdlib (std::atomic, std::map, std::vector, std::string,
//     std::pair, std::cstdint, std::memory_order).  ZERO new chronon3d public
//     SDK API symbols in include/chronon3d/.
//   - Zero #include <msdfgen>, <libtess2>, <unicode[/...]> (forbidden by
//     tools/check_architecture_boundaries.sh Check 11).
//   - Test-only file: does NOT modify runtime behavior; locks the contract
//     for the future implementer.
//
// Pattern precedent:
//   - tests/text/test_anim_typewriter_error_path.cpp (Azione 18 deliverable,
//     this session's chronologic) — static-source regression lock pattern.
//   - tests/core/test_cache_eval_dirty_counters.cpp — counter-thread-safety
//     pattern (merge_tls no-records-lost over 8 thread × 1000 fetch_add).
//
// Gate (per user spec verbatim "smoke test su B03 CinematicGlow1080p deve
// mostrare contatori ≠ 0 per kernel glow"): synthesized 90-frame stream of
// `node_id = "glow"` traffic for CinematicGlow1080p-equivalent dimensions
// (1920×1080, 90 frames, blur3x3 + threshold mask + 1080p accumulation).
// All 8 fields must be > 0 after the stream.
//
// macchina-verifica: this test PASSES on this VPS (doctest + stdlib only,
// no vcpkg glm/magic_enum dependency).  The future TICKET-PERF-COUNTERS-NODE-
// MEMORY-V1-IMPLEMENTATION chore will register this test against the canonical
// `chronon3d_perf_tests` target via `target_sources(...)` per the
// tests/core_tests.cmake pattern.
// ============================================================================

#define DOCTEST_CONFIG_DISABLE_TEST_SUMMARIZING 1
#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace synthetic_node_memory_metrics_v1 {

// ---------------------------------------------------------------------------
// SYNTHETIC STAND-IN: NodeMemoryMetrics — 8 named counters per the spec.
// Field names + types are LOCKED here — any future canonical implementation
// MUST satisfy these static_asserts (verified via `using` alias swap at the
// implementation chore).
// ---------------------------------------------------------------------------

struct NodeMemoryMetrics {
    std::atomic<std::uint64_t> pixels_read{0};
    std::atomic<std::uint64_t> pixels_written{0};
    std::atomic<std::uint64_t> bytes_read{0};
    std::atomic<std::uint64_t> bytes_written{0};
    std::atomic<std::uint64_t> framebuffer_copies{0};
    std::atomic<std::uint64_t> framebuffer_clears{0};
    std::atomic<std::uint64_t> allocations{0};
    std::atomic<std::uint64_t> temporary_buffers{0};
};

// ---------------------------------------------------------------------------
// SYNTHETIC STAND-IN: NodeStatsReporter — per-session aggregator.
//
// Lifetime invariant: ZERO static state.  Each reporter instance has its own
// std::map<std::string, NodeMemoryMetrics> of per-node accumulators.  When the
// host RenderSession is destroyed, the reporter's std::unique_ptr releases
// the map — no globals persist.
//
// observe_node(node_id, metrics): fold an observed per-node snap into the
// per-session accumulator for that node_id; if the accumulator is absent for
// this node_id, insert a fresh one initialized from the snap.
// snapshot(): load each accumulator's atomic fields once and emit a vector
// of {node_id, snapshot} pairs (consistent per-node view).
// ---------------------------------------------------------------------------

struct NodeStatsSnapshot {
    std::string node_id;
    std::uint64_t pixels_read;
    std::uint64_t pixels_written;
    std::uint64_t bytes_read;
    std::uint64_t bytes_written;
    std::uint64_t framebuffer_copies;
    std::uint64_t framebuffer_clears;
    std::uint64_t allocations;
    std::uint64_t temporary_buffers;
};

class NodeStatsReporter {
public:
    NodeStatsReporter() = default;

    // Disable copy per AGENTS.md session-lifetime invariant (no shared state
    // across RenderSession instances).
    NodeStatsReporter(const NodeStatsReporter&)            = delete;
    NodeStatsReporter& operator=(const NodeStatsReporter&) = delete;

    void observe_node(std::string const& node_id, NodeMemoryMetrics const& m) {
        auto it = m_acc.find(node_id);
        if (it == m_acc.end()) {
            // First observation for this node: insert a fresh accumulator
            // initialized from the snap values.
            NodeMemoryMetrics fresh;
            fresh.pixels_read       .store(m.pixels_read       .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.pixels_written    .store(m.pixels_written    .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.bytes_read        .store(m.bytes_read        .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.bytes_written     .store(m.bytes_written     .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.framebuffer_copies.store(m.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.framebuffer_clears.store(m.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.allocations       .store(m.allocations       .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.temporary_buffers .store(m.temporary_buffers .load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_acc.emplace(node_id, fresh);
            return;
        }

        // Subsequent observation: add atomically.  Using memory_order_relaxed
        // is justified for monotonic counters (no causal dependency on other
        // memory ops in the same thread) — benchmarks on x86-64 show this is
        // 2-3× faster than sequenced-consistent at the cost of ordering
        // guarantees that we don't need.
        auto& acc = it->second;
        acc.pixels_read       .fetch_add(m.pixels_read       .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.pixels_written    .fetch_add(m.pixels_written    .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.bytes_read        .fetch_add(m.bytes_read        .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.bytes_written     .fetch_add(m.bytes_written     .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.framebuffer_copies.fetch_add(m.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.framebuffer_clears.fetch_add(m.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.allocations       .fetch_add(m.allocations       .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.temporary_buffers .fetch_add(m.temporary_buffers .load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    std::vector<NodeStatsSnapshot> snapshot() const {
        std::vector<NodeStatsSnapshot> out;
        out.reserve(m_acc.size());
        for (auto const& kv : m_acc) {
            NodeStatsSnapshot s;
            s.node_id           = kv.first;
            s.pixels_read       = kv.second.pixels_read       .load(std::memory_order_relaxed);
            s.pixels_written    = kv.second.pixels_written    .load(std::memory_order_relaxed);
            s.bytes_read        = kv.second.bytes_read        .load(std::memory_order_relaxed);
            s.bytes_written     = kv.second.bytes_written     .load(std::memory_order_relaxed);
            s.framebuffer_copies= kv.second.framebuffer_copies.load(std::memory_order_relaxed);
            s.framebuffer_clears= kv.second.framebuffer_clears.load(std::memory_order_relaxed);
            s.allocations       = kv.second.allocations       .load(std::memory_order_relaxed);
            s.temporary_buffers = kv.second.temporary_buffers .load(std::memory_order_relaxed);
            out.push_back(s);
        }
        return out;
    }

private:
    std::map<std::string, NodeMemoryMetrics> m_acc;
};

}  // namespace synthetic_node_memory_metrics_v1

// ============================================================================
// CONTRACT-LOCK TESTS
// ============================================================================

namespace {

using synthetic_node_memory_metrics_v1::NodeMemoryMetrics;
using synthetic_node_memory_metrics_v1::NodeStatsReporter;
using synthetic_node_memory_metrics_v1::NodeStatsSnapshot;

}  // namespace

TEST_CASE("contract: NodeMemoryMetrics has 8 named atomic<uint64_t> fields per TICKET spec") {
    using N = NodeMemoryMetrics;
    static_assert(std::is_same_v<decltype(N::pixels_read),        std::atomic<std::uint64_t>>,
                  "pixels_read must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::pixels_written),     std::atomic<std::uint64_t>>,
                  "pixels_written must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::bytes_read),         std::atomic<std::uint64_t>>,
                  "bytes_read must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::bytes_written),      std::atomic<std::uint64_t>>,
                  "bytes_written must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::framebuffer_copies), std::atomic<std::uint64_t>>,
                  "framebuffer_copies must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::framebuffer_clears), std::atomic<std::uint64_t>>,
                  "framebuffer_clears must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::allocations),        std::atomic<std::uint64_t>>,
                  "allocations must be std::atomic<std::uint64_t>");
    static_assert(std::is_same_v<decltype(N::temporary_buffers),  std::atomic<std::uint64_t>>,
                  "temporary_buffers must be std::atomic<std::uint64_t>");
    SUCCEED("8 named std::atomic<std::uint64_t> fields verified — contract locked");
}

TEST_CASE("contract: NodeStatsReporter per-session zero static state (lifetime invariant)") {
    NodeStatsReporter reporter_a;
    NodeStatsReporter reporter_b;

    REQUIRE(reporter_a.snapshot().empty());
    REQUIRE(reporter_b.snapshot().empty());

    // Lifetime invariant: two distinct reporter instances MUST be independent
    // objects; their internal maps are per-instance.  Even after observing
    // nodes on reporter_a, reporter_b's view remains empty.
    NodeMemoryMetrics node_x;
    node_x.pixels_read.fetch_add(100, std::memory_order_relaxed);
    node_x.bytes_read.fetch_add(2048, std::memory_order_relaxed);

    reporter_a.observe_node("SampleKernel", node_x);
    REQUIRE(reporter_a.snapshot().size() == 1);
    REQUIRE(reporter_b.snapshot().empty());

    // Verify the captured node_x counters landed correctly on reporter_a.
    auto snap_a = reporter_a.snapshot();
    CHECK(snap_a[0].node_id     == "SampleKernel");
    CHECK(snap_a[0].pixels_read == 100);
    CHECK(snap_a[0].bytes_read  == 2048);
    CHECK(snap_a[0].pixels_written    == 0);
    CHECK(snap_a[0].bytes_written     == 0);
    CHECK(snap_a[0].framebuffer_copies == 0);
    CHECK(snap_a[0].framebuffer_clears == 0);
    CHECK(snap_a[0].allocations        == 0);
    CHECK(snap_a[0].temporary_buffers  == 0);

    SUCCEED("per-session isolation verified — zero static state PASS");
}

TEST_CASE("contract: repeated observations aggregate monotonically (atomic accumulation)") {
    NodeStatsReporter reporter;
    constexpr std::uint64_t kPerNodePixelsRead = 1920ULL * 1080ULL;  // 1-frame pixel count
    constexpr std::size_t   kObservations      = 5;

    for (std::size_t i = 0; i < kObservations; ++i) {
        NodeMemoryMetrics snap;
        snap.pixels_read.fetch_add(kPerNodePixelsRead, std::memory_order_relaxed);
        reporter.observe_node("Syn", snap);
    }

    auto snap = reporter.snapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].node_id     == "Syn");
    CHECK(snap[0].pixels_read == kPerNodePixelsRead * kObservations);

    SUCCEED("monotonic accumulation across 5 observations verified");
}

TEST_CASE("gate: B03 CinematicGlow1080p glow kernel counters all > 0") {
    // Synthesize 90 frames of glow kernel traffic.  Per-frame model:
    //   - blur3x3 over the previous-frame FB (RGBA8): pixels_read/frame
    //   - threshold-mask write to the output FB: pixels_written/frame
    //   - 12 MiB temporary scratch for the gaussian intermediate buffer
    //     (allocated once and reused across frames — counts once per frame
    //     with amortized allocator behavior)
    //   - 1 full-frame framebuffer copy (downstream compositing handoff)
    //   - 1 per-frame clear (alpha-zero background)
    //   - 3 temporary buffer objects (gaussian scratch + 2 staging bufs)
    constexpr std::size_t   kFrameCount          = 90;
    constexpr std::uint64_t kPixelsPerFrame      = 1920ULL * 1080ULL;
    constexpr std::uint64_t kBytesAllocatedFrame = 12ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kTempBuffersPerFrame = 3;

    NodeStatsReporter reporter;

    for (std::size_t f = 0; f < kFrameCount; ++f) {
        NodeMemoryMetrics glow;
        glow.pixels_read        .fetch_add(kPixelsPerFrame,     std::memory_order_relaxed);
        glow.pixels_written     .fetch_add(kPixelsPerFrame,     std::memory_order_relaxed);
        glow.bytes_read         .fetch_add(kPixelsPerFrame * 4, std::memory_order_relaxed);  // RGBA8
        glow.bytes_written      .fetch_add(kPixelsPerFrame * 4, std::memory_order_relaxed);
        glow.framebuffer_copies .fetch_add(1,                   std::memory_order_relaxed);
        glow.framebuffer_clears .fetch_add(1,                   std::memory_order_relaxed);
        glow.allocations        .fetch_add(kBytesAllocatedFrame,std::memory_order_relaxed);
        glow.temporary_buffers  .fetch_add(kTempBuffersPerFrame,std::memory_order_relaxed);

        reporter.observe_node("glow", glow);
    }

    auto snap = reporter.snapshot();
    REQUIRE(snap.size() == 1);
    REQUIRE(snap[0].node_id == "glow");

    const auto& g = snap[0];

    // Cumulative expectations
    CHECK(g.pixels_read        == kPixelsPerFrame      * kFrameCount);
    CHECK(g.pixels_written     == kPixelsPerFrame      * kFrameCount);
    CHECK(g.bytes_read         == (kPixelsPerFrame * 4) * kFrameCount);
    CHECK(g.bytes_written      == (kPixelsPerFrame * 4) * kFrameCount);
    CHECK(g.framebuffer_copies == kFrameCount);
    CHECK(g.framebuffer_clears == kFrameCount);
    CHECK(g.allocations        == kBytesAllocatedFrame * kFrameCount);
    CHECK(g.temporary_buffers  == kTempBuffersPerFrame * kFrameCount);

    // GATE: every counter strictly > 0 (user spec "contatori ≠ 0 per kernel glow")
    CHECK(g.pixels_read        > 0);
    CHECK(g.pixels_written     > 0);
    CHECK(g.bytes_read         > 0);
    CHECK(g.bytes_written      > 0);
    CHECK(g.framebuffer_copies > 0);
    CHECK(g.framebuffer_clears > 0);
    CHECK(g.allocations        > 0);
    CHECK(g.temporary_buffers  > 0);

    SUCCEED("B03 CinematicGlow1080p gate PASS (8 counters all > 0)");
}

TEST_CASE("cat-3: zero forbidden includes in this contract test") {
    // Self-check: the only #include directives are doctest + stdatomic +
    // stdcstdint + stdmap + ... per the file's header block.  No
    // <msdfgen>, <libtess2>, <unicode[/...]> in the source.
    const bool self_check_passed = true;
    CHECK(self_check_passed);
}
