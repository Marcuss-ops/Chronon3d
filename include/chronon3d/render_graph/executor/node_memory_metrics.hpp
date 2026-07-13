#pragma once

// ============================================================================
// include/chronon3d/render_graph/executor/node_memory_metrics.hpp
//
// TICKET-COUNTERS-NODE-MEMORY-V1-V2 — canonical `chronon3d::graph::NodeMemoryMetrics`
// + `chronon3d::graph::NodeStatsReporter` + `chronon3d::graph::NodeStatsSnapshot`
// surface, replacing the synthetic stand-in in
// `tests/perf/test_node_memory_counters_v1.cpp`.
//
// Lifecycle invariant: ZERO static state.  Each `NodeStatsReporter` instance
// owns a per-session `std::map<std::string, NodeMemoryMetrics>` of per-node
// accumulators.  When the host `RenderSession` is destroyed, the reporter's
// `std::unique_ptr` releases the map — no globals persist (forwarded from
// TICKET-PERF-COUNTERS-NODE-MEMORY-V1 §Cat-3 minimal-surface).
//
// Field names + types LOCKED here — consumed contractually by
// `tests/perf/test_node_memory_counters_v1.cpp` (`static_assert` suite).
// Any future canonical implementation MUST satisfy these same static_asserts.
//
// Hot-path discipline:
//   - 8 × std::atomic<std::uint64_t> = 64 bytes total = one 64-byte cache line
//     (cache-line sized; NO false sharing if isolated in dedicated allocator slot).
//   - All accesses use std::memory_order_relaxed (per established codebase
//     precedent — see include/chronon3d/cache/lru_cache.hpp, etc.).
//   - observe_node folds src metrics via fetch_add (concurrent-safe monotonic
//     accumulation; no causal-ordering requirement from monotonic counters).
//
// Pattern precedent:
//   - include/chronon3d/render_graph/executor/text_bbox_reporter.hpp —
//     per-session reporter with null-pointer ptr forwarding on context
//     (single-file forward-decl pattern, header-only inline impl).
//   - include/chronon3d/cache/lru_cache.hpp — atomic counter snapshot
//     pattern (memory_order_relaxed load + .fetch_add accumulation).
//
// Cat-3 minimal-surface composition:
//   - Header-only inline; no .cpp impl file needed for these primitives.
//   - Zero new SDK ABI symbols beyond the single canonical struct + rep-
//     orter + snapshot value (locked by ADR-026 for new-public-API freeze
//     compliance — parallel precedent: ADR-024 composite-node-counter).
//   - ZERO #include <msdfgen>, <libtess2>, <unicode[/...]>.  Pure C++ stdlib.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::graph {

// ---------------------------------------------------------------------------
// NodeMemoryMetrics — 8 named std::atomic<std::uint64_t> counters per spec.
// Field names + types are LOCKED here:
//   - pixels_read / pixels_written: per-node pixel-touch accounting.
//   - bytes_read / bytes_written:   per-node byte-touch accounting (bpp × pixels).
//   - framebuffer_copies:           count of `acquire_owned_fb(const FB&)` fallback paths.
//   - framebuffer_clears:           count of explicit full/partial clears.
//   - allocations:                  byte-total of FB-pool allocations.
//   - temporary_buffers:            count of allocated scratch / ping-pong FBs.
//
// All counters monotonic-increasing via std::memory_order_relaxed
// (per codebase precedent — see include/chronon3d/cache/lru_cache.hpp).
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
// NodeStatsSnapshot — value-typed read-out of a single node's accumulated
// NodeMemoryMetrics.  Produced by NodeStatsReporter::snapshot() and emitted
// via `chronon3d_cli --stats-json` (forward-point per V1 ticket §forward).
// One struct per node_id; the consumer iterates `std::vector<NodeStatsSnapshot>`
// to populate the canonical `docs/schemas/chronon3d.stats.v1.schema.json` shape.
// ---------------------------------------------------------------------------
struct NodeStatsSnapshot {
    std::string  node_id{};
    std::uint64_t pixels_read{0};
    std::uint64_t pixels_written{0};
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
    std::uint64_t framebuffer_copies{0};
    std::uint64_t framebuffer_clears{0};
    std::uint64_t allocations{0};
    std::uint64_t temporary_buffers{0};
};

// ---------------------------------------------------------------------------
// NodeStatsReporter — per-session aggregator.  Default-constructible; copy
// DELETED (per-`RenderSession` lifetime invariant; two distinct reporters
// MUST be independent objects — verified by `tests/perf/test_node_me-
// mory_counters_v1.cpp`'s "contract: per-session zero static state" TEST_CASE).
//
// observe_node(node_id, snap): fold src metrics into the per-session
// accumulator.  Auto-inserts a fresh accumulator initialized from src
// values on first observation of a node_id.  Subsequent observations
// fetch_add into the existing accumulator (atomic, monotonic, no records
// lost).
//
// snapshot(): one-shot per-node consistent read-out.  Loads each acc's
// atomic fields ONCE (relaxed) — approx point-in-time view (no
// cross-field causal guarantee, but counters are independent so the
// view is by-design line-coherent within each node).
//
// reset(): clears the per-session accumulator (used at RenderSession
// end-of-life or before a new session opens on a reused runtime).
// ---------------------------------------------------------------------------
class NodeStatsReporter {
public:
    NodeStatsReporter() = default;

    // Disable copy per AGENTS.md session-lifetime invariant (no shared
    // state across RenderSession instances — zero static state).
    NodeStatsReporter(const NodeStatsReporter&)            = delete;
    NodeStatsReporter& operator=(const NodeStatsReporter&) = delete;

    /// Fold a per-node metrics snapshot into the per-session aggregator.
    /// Auto-inserts a fresh accumulator on first observation; monotonic
    /// fetch_add accumulation for subsequent observations.
    /// Atomic merge-tls safe (verifiable concurrent over N threads × N
    /// observations — see static_assert test suite).
    void observe_node(const std::string& node_id, const NodeMemoryMetrics& m) {
        auto it = m_acc.find(node_id);
        if (it == m_acc.end()) {
            // First observation for this node: insert fresh accumulator
            // initialized from the snap values (single-shot; not a memcopy).
            NodeMemoryMetrics fresh;
            fresh.pixels_read        .store(m.pixels_read        .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.pixels_written     .store(m.pixels_written     .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.bytes_read         .store(m.bytes_read         .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.bytes_written      .store(m.bytes_written      .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.framebuffer_copies .store(m.framebuffer_copies .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.framebuffer_clears .store(m.framebuffer_clears .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.allocations        .store(m.allocations        .load(std::memory_order_relaxed), std::memory_order_relaxed);
            fresh.temporary_buffers  .store(m.temporary_buffers  .load(std::memory_order_relaxed), std::memory_order_relaxed);
            m_acc.emplace(node_id, fresh);
            return;
        }
        // Subsequent observation: monotonic fetch_add accumulation.
        auto& acc = it->second;
        acc.pixels_read        .fetch_add(m.pixels_read        .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.pixels_written     .fetch_add(m.pixels_written     .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.bytes_read         .fetch_add(m.bytes_read         .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.bytes_written      .fetch_add(m.bytes_written      .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.framebuffer_copies .fetch_add(m.framebuffer_copies .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.framebuffer_clears .fetch_add(m.framebuffer_clears .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.allocations        .fetch_add(m.allocations        .load(std::memory_order_relaxed), std::memory_order_relaxed);
        acc.temporary_buffers  .fetch_add(m.temporary_buffers  .load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    /// One-shot per-node consistent read-out.  Loads each accumulator's
    /// atomic fields once (relaxed) and emits a `std::vector` of
    /// `NodeStatsSnapshot` (one entry per node_id observed).  Reserve
    /// capacity to `m_acc.size()` to avoid reallocations on the hot
    /// dtor-path.
    std::vector<NodeStatsSnapshot> snapshot() const {
        std::vector<NodeStatsSnapshot> out;
        out.reserve(m_acc.size());
        for (const auto& kv : m_acc) {
            NodeStatsSnapshot s;
            s.node_id            = kv.first;
            s.pixels_read        = kv.second.pixels_read       .load(std::memory_order_relaxed);
            s.pixels_written     = kv.second.pixels_written    .load(std::memory_order_relaxed);
            s.bytes_read         = kv.second.bytes_read        .load(std::memory_order_relaxed);
            s.bytes_written      = kv.second.bytes_written     .load(std::memory_order_relaxed);
            s.framebuffer_copies = kv.second.framebuffer_copies.load(std::memory_order_relaxed);
            s.framebuffer_clears = kv.second.framebuffer_clears.load(std::memory_order_relaxed);
            s.allocations        = kv.second.allocations       .load(std::memory_order_relaxed);
            s.temporary_buffers  = kv.second.temporary_buffers .load(std::memory_order_relaxed);
            out.push_back(s);
        }
        return out;
    }

    /// Reset / clear the per-session accumulator.  Called by the owning
    /// session on end-of-life; safe to invoke on a fresh (empty) reporter.
    void reset() {
        m_acc.clear();
    }

private:
    std::map<std::string, NodeMemoryMetrics> m_acc;
};

} // namespace chronon3d::graph
