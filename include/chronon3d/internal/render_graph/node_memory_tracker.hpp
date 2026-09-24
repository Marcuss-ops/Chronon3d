#pragma once

#include <chronon3d/core/profiling/profiling_context.hpp>  // g_current_counters
#include <chronon3d/core/profiling/render_counter_types.hpp>  // RenderCounters
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/render_graph/executor/node_memory_metrics.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::graph {

struct NodeMemoryPoolSnapshot {
    std::uint64_t current_bytes{0};
    std::uint64_t retained_bytes{0};
    std::uint64_t peak_retained_bytes{0};
    std::uint64_t total_allocations{0};
    std::uint64_t total_reuses{0};
    std::uint64_t total_returns{0};
    std::uint64_t evicted_bytes{0};
};

struct NodeMemorySampleSnapshot {
    TemporalSampleKey sample_key{};
    /// Temporary bytes observed for this sample, including reused buffers.
    std::uint64_t temporary_bytes_observed{0};
    std::uint64_t temporary_buffers{0};
    std::uint64_t live_bytes{0};
    std::uint64_t peak_live_bytes{0};
};

struct NodeMemoryReport {
    std::vector<NodeStatsSnapshot> nodes;
    std::vector<NodeMemorySampleSnapshot> samples;
    NodeMemoryPoolSnapshot framebuffer_pool{};
    std::uint64_t current_live_bytes{0};
    std::uint64_t peak_live_bytes{0};
    std::uint64_t peak_rss_bytes{0};
};

/// Internal per-session tracker. One instance belongs to one runtime::JobSession;
/// concurrent graph executions serialize updates through this tracker, while
/// TemporalSampleKey keeps sample domains distinct. The owning session may
/// call reset() at an explicit report-lifetime boundary.
///
/// HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — acquire/release restructuring.
///
/// The previous implementation paid, PER acquire_temporary/release_temporary
/// call (once per node × frame whenever a session tracker is attached, which
/// is always):
///   * 7–8 std::string(node_id) constructions — heap traffic for any id
///     beyond the SSO limit (the canonical "g<n>:n<m>" form reaches 43 chars):
///     1 in the ScopedNodeMemory constructor copy, 4 in acquire_temporary
///     (m_live_nodes, m_peak_live_nodes, reporter observe_node, reporter
///     observe_live_bytes) and 3 in release_temporary;
///   * 2–3 separate std::lock_guard acquisitions (m_mutex for the live/peak
///     maps, then the NodeStatsReporter's internal mutex per observe_node /
///     observe_live_bytes call);
///   * 2–4 std::map<std::string, …> red-black-tree lookups with full string
///     comparisons and pointer chasing.
/// The restructure keeps every externally observable value IDENTICAL
/// (contract-verified against tests/perf/test_node_memory_tracker.cpp and
/// the motion-blur sample-domain test):
///   1. The reporter's node table and the two live/peak maps are folded into
///      ONE map with a transparent (std::less<>) comparator: the steady-state
///      path performs a single std::string_view-keyed find with ZERO string
///      constructions. Only the FIRST observation of a distinct id pays one
///      std::string allocation (insert) under the mutex.
///   2. One mutex acquisition per call (was 2–3) and one map lookup (was
///      2–4). The NodeStatsReporter's per-observe mutex is no longer on this
///      path at all.
///   3. ScopedNodeMemory keeps storing a std::string copy (the executor
///      passes a temporary produced by memory_node_id(), so a borrowed view
///      would dangle). With the SSO-sized "g<n>:n<m>" ids the copy is
///      typically allocation-free; the tracker-internal copies are gone.
///   4. All values — process-wide current/peak, per-node live/peak, sample
///      domains, monotonic counters, reset/merge/snapshot semantics — are
///      exactly what the old code produced for the same call sequence.
class NodeMemoryTracker {
public:
    NodeMemoryTracker() = default;
    NodeMemoryTracker(const NodeMemoryTracker&) = delete;
    NodeMemoryTracker& operator=(const NodeMemoryTracker&) = delete;

    /// Reserve the per-node aggregate before the execution scope enters its
    /// noexcept destructor path. Subsequent observations for this key update
    /// the existing row without allocating.
    void ensure_node(std::string_view node_id, std::string_view node_type = {}) {
        const MeteredLock lock(m_mutex);
        NodeLiveState& node = find_or_emplace(node_id);
        if (node.node_type.empty() && !node_type.empty()) {
            node.node_type.assign(node_type);
        }
    }

    void observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics) {
        const MeteredLock lock(m_mutex);
        NodeLiveState& node = find_or_emplace(node_id);
        // Monotonic counters fold additively; live/peak are point-in-time
        // state updated exclusively by the acquire/release path (same
        // contract NodeStatsReporter::add_into documented).
        node.metrics.pixels_read.fetch_add(
            metrics.pixels_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.pixels_written.fetch_add(
            metrics.pixels_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.bytes_read.fetch_add(
            metrics.bytes_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.bytes_written.fetch_add(
            metrics.bytes_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.framebuffer_copies.fetch_add(
            metrics.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.framebuffer_clears.fetch_add(
            metrics.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.allocations.fetch_add(
            metrics.allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.allocated_bytes.fetch_add(
            metrics.allocated_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node.metrics.temporary_buffers.fetch_add(
            metrics.temporary_buffers.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void acquire_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes)
    {
        const MeteredLock lock(m_mutex);
        NodeLiveState& node = find_or_emplace(node_id);
        node.metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);

        node.live_bytes += bytes;
        node.peak_live_bytes = std::max(node.peak_live_bytes, node.live_bytes);

        m_current_live_bytes += bytes;
        m_peak_live_bytes = std::max(m_peak_live_bytes, m_current_live_bytes);

        if (sample_key) {
            auto& sample = m_samples[*sample_key];
            sample.sample_key = *sample_key;
            sample.temporary_bytes_observed += bytes;
            sample.temporary_buffers += 1;
            sample.live_bytes += bytes;
            sample.peak_live_bytes = std::max(sample.peak_live_bytes, sample.live_bytes);
        }
    }

    void release_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes)
    {
        const MeteredLock lock(m_mutex);
        // Every release is paired with an acquire that registered the id;
        // find_or_emplace keeps an unpaired release safe rather than UB.
        NodeLiveState& node = find_or_emplace(node_id);
        subtract_saturating(node.live_bytes, bytes);
        subtract_saturating(m_current_live_bytes, bytes);
        if (sample_key) {
            auto sample = m_samples.find(*sample_key);
            if (sample != m_samples.end()) {
                subtract_saturating(sample->second.live_bytes, bytes);
            }
        }
    }

    void record_allocation(std::string_view node_id, std::uint64_t bytes) {
        const MeteredLock lock(m_mutex);
        NodeLiveState& node = find_or_emplace(node_id);
        node.metrics.allocations.fetch_add(1, std::memory_order_relaxed);
        node.metrics.allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }

    void record_pool(NodeMemoryPoolSnapshot pool) {
        const MeteredLock lock(m_mutex);
        m_pool = pool;
    }

    void record_rss_peak(std::uint64_t rss_bytes) {
        const MeteredLock lock(m_mutex);
        m_peak_rss_bytes = std::max(m_peak_rss_bytes, rss_bytes);
    }

    /// Merge a completed isolated sample report into the owning render
    /// session. Temporal sample sessions are intentionally short-lived, so
    /// their counters must be transferred before the sample domain is
    /// destroyed; this does not merge any cache or framebuffer ownership.
    void merge_report(const NodeMemoryReport& report) {
        const MeteredLock lock(m_mutex);
        for (const auto& node : report.nodes) {
            NodeLiveState& dst = find_or_emplace(node.node_id);
            if (dst.node_type.empty()) dst.node_type = node.node_type;
            dst.metrics.pixels_read.fetch_add(node.pixels_read, std::memory_order_relaxed);
            dst.metrics.pixels_written.fetch_add(node.pixels_written, std::memory_order_relaxed);
            dst.metrics.bytes_read.fetch_add(node.bytes_read, std::memory_order_relaxed);
            dst.metrics.bytes_written.fetch_add(node.bytes_written, std::memory_order_relaxed);
            dst.metrics.framebuffer_copies.fetch_add(node.framebuffer_copies, std::memory_order_relaxed);
            dst.metrics.framebuffer_clears.fetch_add(node.framebuffer_clears, std::memory_order_relaxed);
            dst.metrics.allocations.fetch_add(node.allocations, std::memory_order_relaxed);
            dst.metrics.allocated_bytes.fetch_add(node.allocated_bytes, std::memory_order_relaxed);
            dst.metrics.temporary_buffers.fetch_add(node.temporary_buffers, std::memory_order_relaxed);
            dst.live_bytes = node.live_bytes;
            dst.peak_live_bytes = std::max(dst.peak_live_bytes, node.peak_live_bytes);
        }
        for (const auto& sample : report.samples) {
            auto& dst = m_samples[sample.sample_key];
            dst.sample_key = sample.sample_key;
            dst.temporary_bytes_observed += sample.temporary_bytes_observed;
            dst.temporary_buffers += sample.temporary_buffers;
            dst.live_bytes = sample.live_bytes;
            dst.peak_live_bytes = std::max(dst.peak_live_bytes, sample.peak_live_bytes);
        }
        m_pool.current_bytes = std::max(m_pool.current_bytes, report.framebuffer_pool.current_bytes);
        m_pool.retained_bytes = std::max(m_pool.retained_bytes, report.framebuffer_pool.retained_bytes);
        m_pool.peak_retained_bytes = std::max(m_pool.peak_retained_bytes, report.framebuffer_pool.peak_retained_bytes);
        m_pool.total_allocations += report.framebuffer_pool.total_allocations;
        m_pool.total_reuses += report.framebuffer_pool.total_reuses;
        m_pool.total_returns += report.framebuffer_pool.total_returns;
        m_pool.evicted_bytes += report.framebuffer_pool.evicted_bytes;
        m_current_live_bytes = std::max(m_current_live_bytes, report.current_live_bytes);
        m_peak_live_bytes = std::max(m_peak_live_bytes, report.peak_live_bytes);
        m_peak_rss_bytes = std::max(m_peak_rss_bytes, report.peak_rss_bytes);
    }

    [[nodiscard]] NodeMemoryReport snapshot() const {
        const MeteredLock lock(m_mutex);
        NodeMemoryReport report;
        report.nodes.reserve(m_nodes.size());
        for (const auto& [node_id, node] : m_nodes) {
            NodeStatsSnapshot snap{};
            snap.node_id = node_id;
            snap.node_type = node->node_type;
            snap.pixels_read = node->metrics.pixels_read.load(std::memory_order_relaxed);
            snap.pixels_written = node->metrics.pixels_written.load(std::memory_order_relaxed);
            snap.bytes_read = node->metrics.bytes_read.load(std::memory_order_relaxed);
            snap.bytes_written = node->metrics.bytes_written.load(std::memory_order_relaxed);
            snap.framebuffer_copies = node->metrics.framebuffer_copies.load(std::memory_order_relaxed);
            snap.framebuffer_clears = node->metrics.framebuffer_clears.load(std::memory_order_relaxed);
            snap.allocations = node->metrics.allocations.load(std::memory_order_relaxed);
            snap.allocated_bytes = node->metrics.allocated_bytes.load(std::memory_order_relaxed);
            snap.temporary_buffers = node->metrics.temporary_buffers.load(std::memory_order_relaxed);
            snap.live_bytes = node->live_bytes;
            snap.peak_live_bytes = node->peak_live_bytes;
            report.nodes.push_back(std::move(snap));
        }
        report.samples.reserve(m_samples.size());
        for (const auto& [key, sample] : m_samples) report.samples.push_back(sample);
        report.framebuffer_pool = m_pool;
        report.current_live_bytes = m_current_live_bytes;
        report.peak_live_bytes = m_peak_live_bytes;
        report.peak_rss_bytes = m_peak_rss_bytes;
        return report;
    }

    void reset() {
        const MeteredLock lock(m_mutex);
        m_nodes.clear();
        m_samples.clear();
        m_pool = {};
        m_current_live_bytes = 0;
        m_peak_live_bytes = 0;
        m_peak_rss_bytes = 0;
    }

private:
    /// Lock metering for the tracker's single mutex.
    ///
    /// The tracker is attached to every session, so an `observe_node` /
    /// `acquire_temporary` / `release_temporary` pair per node × frame means
    /// thousands of acquisitions per second against ONE mutex shared by all
    /// level workers. The P1 decision ("restructure, or leave it alone") must
    /// be data-driven, so every acquisition records:
    ///   * node_memory_lock_acquires      — acquisitions
    ///   * node_memory_lock_wait_us       — total time spent waiting
    ///   * node_memory_lock_max_wait_us   — worst single wait (CAS high-water)
    ///   * node_memory_lock_hold_wall_us  — total time the mutex was held
    /// The counters live in the thread-local counter block, so metering adds no
    /// shared state and no cache-line bouncing of its own.
    class MeteredLock {
    public:
        explicit MeteredLock(std::mutex& mutex)
            : mutex_(mutex), wait_t0_(std::chrono::steady_clock::now()) {
            mutex_.lock();
            hold_t0_ = std::chrono::steady_clock::now();
            auto* counters = profiling::g_current_counters;
            if (counters == nullptr) return;
            const auto wait_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    hold_t0_ - wait_t0_).count());
            counters->node_memory_lock_acquires.fetch_add(
                1, std::memory_order_relaxed);
            counters->node_memory_lock_wait_us.fetch_add(
                wait_us, std::memory_order_relaxed);
            record_max(counters->node_memory_lock_max_wait_us, wait_us);
        }

        ~MeteredLock() {
            mutex_.unlock();
            auto* counters = profiling::g_current_counters;
            if (counters == nullptr) return;
            counters->node_memory_lock_hold_wall_us.fetch_add(
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - hold_t0_).count()),
                std::memory_order_relaxed);
        }

        MeteredLock(const MeteredLock&) = delete;
        MeteredLock& operator=(const MeteredLock&) = delete;

    private:
        static void record_max(std::atomic<std::uint64_t>& sink,
                               std::uint64_t value) noexcept {
            auto current = sink.load(std::memory_order_relaxed);
            while (value > current &&
                   !sink.compare_exchange_weak(current, value,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed)) {
            }
        }

        std::mutex& mutex_;
        std::chrono::steady_clock::time_point wait_t0_{};
        std::chrono::steady_clock::time_point hold_t0_{};
    };

    struct SampleKeyLess {
        bool operator()(const TemporalSampleKey& a, const TemporalSampleKey& b) const noexcept {
            if (a.frame != b.frame) return a.frame < b.frame;
            if (a.subframe_tick != b.subframe_tick) return a.subframe_tick < b.subframe_tick;
            return a.version < b.version;
        }
    };

    /// One per-node record: monotonic counters + point-in-time live state.
    /// Heap-indirected via unique_ptr because NodeMemoryMetrics carries
    /// std::atomic members (non-movable), so the map value must be
    /// move-relocatable. One allocation per DISTINCT node id, never per call.
    struct NodeLiveState {
        NodeMemoryMetrics metrics{};
        std::string node_type;
        std::uint64_t live_bytes{0};
        std::uint64_t peak_live_bytes{0};
    };

    /// Transparent comparator enabling C++14 heterogeneous lookup: find by
    /// string_view without constructing a std::string key.
    struct StringViewLess {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a < b;
        }
    };

    /// Single find-or-insert for the folded node table. Caller holds m_mutex.
    [[nodiscard]] NodeLiveState& find_or_emplace(std::string_view node_id) {
        auto it = m_nodes.find(node_id);
        if (it != m_nodes.end()) {
            return *it->second;
        }
        auto fresh = std::make_unique<NodeLiveState>();
        NodeLiveState& ref = *fresh;
        m_nodes.emplace(std::string(node_id), std::move(fresh));
        return ref;
    }

    static void subtract_saturating(std::uint64_t& value, std::uint64_t amount) noexcept {
        value = amount > value ? 0 : value - amount;
    }

    mutable std::mutex m_mutex;
    std::map<std::string, std::unique_ptr<NodeLiveState>, StringViewLess> m_nodes;
    std::map<TemporalSampleKey, NodeMemorySampleSnapshot, SampleKeyLess> m_samples;
    NodeMemoryPoolSnapshot m_pool{};
    std::uint64_t m_current_live_bytes{0};
    std::uint64_t m_peak_live_bytes{0};
    std::uint64_t m_peak_rss_bytes{0};
};

/// RAII live-memory scope. It records a separate allocation event only when
/// record_allocation() is explicitly requested, so borrowed/reused buffers do
/// not inflate allocation counters.
///
/// The node id is stored as a std::string copy: the canonical construction
/// site (execute_single_node) passes the temporary returned by
/// memory_node_id(), so a borrowed view would dangle. Ids are "g<n>:n<m>"
/// composites that typically fit the SSO buffer, making the copy
/// allocation-free in practice.
class ScopedNodeMemory {
public:
    ScopedNodeMemory(
        NodeMemoryTracker& tracker,
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes,
        std::string_view node_type = {})
        : m_tracker(&tracker), m_node_id(node_id), m_sample_key(sample_key), m_bytes(bytes)
    {
        m_tracker->ensure_node(m_node_id, node_type);
        if (m_bytes != 0) m_tracker->acquire_temporary(m_node_id, m_sample_key, m_bytes);
        m_previous_sink = profiling::g_node_memory_event_sink;
        m_event_sink = profiling::NodeMemoryEventSink{
            .context = this,
            .record = &ScopedNodeMemory::record_event,
        };
        profiling::g_node_memory_event_sink = &m_event_sink;
    }

    ~ScopedNodeMemory() {
        profiling::g_node_memory_event_sink = m_previous_sink;
        if (m_tracker) m_tracker->observe_node(m_node_id, m_metrics);
        if (m_tracker && m_bytes != 0) {
            m_tracker->release_temporary(m_node_id, m_sample_key, m_bytes);
        }
    }

    ScopedNodeMemory(const ScopedNodeMemory&) = delete;
    ScopedNodeMemory& operator=(const ScopedNodeMemory&) = delete;

    void set_live_bytes(std::uint64_t bytes) {
        if (!m_tracker || bytes == m_bytes) return;
        if (m_bytes != 0) m_tracker->release_temporary(m_node_id, m_sample_key, m_bytes);
        m_bytes = bytes;
        if (m_bytes != 0) {
            m_tracker->acquire_temporary(m_node_id, m_sample_key, m_bytes);
            m_metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void record_allocation(std::uint64_t bytes) {
        if (m_tracker) m_tracker->record_allocation(m_node_id, bytes);
    }

    void record_read(std::uint64_t pixels, std::uint64_t bytes) noexcept {
        m_metrics.pixels_read.fetch_add(pixels, std::memory_order_relaxed);
        m_metrics.bytes_read.fetch_add(bytes, std::memory_order_relaxed);
    }

    void record_write(std::uint64_t pixels, std::uint64_t bytes) noexcept {
        m_metrics.pixels_written.fetch_add(pixels, std::memory_order_relaxed);
        m_metrics.bytes_written.fetch_add(bytes, std::memory_order_relaxed);
    }

    [[nodiscard]] NodeMemoryMetrics metrics_snapshot() const noexcept {
        return m_metrics;
    }

private:
    static void record_event(
        void* context,
        profiling::NodeMemoryEvent event,
        std::uint64_t bytes) noexcept {
        auto& metrics = static_cast<ScopedNodeMemory*>(context)->m_metrics;
        switch (event) {
        case profiling::NodeMemoryEvent::Allocation:
            metrics.allocations.fetch_add(1, std::memory_order_relaxed);
            metrics.allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
            break;
        case profiling::NodeMemoryEvent::Copy:
            metrics.framebuffer_copies.fetch_add(1, std::memory_order_relaxed);
            break;
        case profiling::NodeMemoryEvent::Clear:
            metrics.framebuffer_clears.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }

    NodeMemoryTracker* m_tracker;
    std::string m_node_id;
    std::optional<TemporalSampleKey> m_sample_key;
    std::uint64_t m_bytes{0};
    NodeMemoryMetrics m_metrics{};
    profiling::NodeMemoryEventSink m_event_sink{};
    profiling::NodeMemoryEventSink* m_previous_sink{nullptr};
};

} // namespace chronon3d::graph
