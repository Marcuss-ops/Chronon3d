#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/render_graph/executor/node_memory_metrics.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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

/// Internal per-session tracker. One instance belongs to one RenderSession;
/// concurrent graph executions serialize updates through this tracker, while
/// TemporalSampleKey keeps sample domains distinct. The owning session may
/// call reset() at an explicit report-lifetime boundary.
///
/// OPT-TRACKER-LOOKUP — hot-path allocation profile. The tracker is observed
/// once per node × frame from execute_single_node (ScopedNodeMemory), so the
/// observation path is on the render data-plane. The maps therefore use
/// transparent comparators (std::less<>): steady-state observations perform
/// heterogeneous string_view lookups with ZERO heap allocation; the owned
/// key string is constructed only on first sight of a node (insertion).
/// Cross-thread visibility of live bytes is required (snapshot() is read
/// mid-scope by the report path and tests), so observations stay locked —
/// the fused transfer_temporary() halves that lock traffic for the
/// set_live_bytes resize pattern (release + acquire in one acquisition).
class NodeMemoryTracker {
public:
    NodeMemoryTracker() = default;
    NodeMemoryTracker(const NodeMemoryTracker&) = delete;
    NodeMemoryTracker& operator=(const NodeMemoryTracker&) = delete;

    void observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reporter.observe_node(node_id, metrics);
    }

    void acquire_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_live_bytes += bytes;
        m_peak_live_bytes = std::max(m_peak_live_bytes, m_current_live_bytes);
        auto node_it = m_live_nodes.find(node_id);
        if (node_it == m_live_nodes.end()) {
            node_it = m_live_nodes.emplace(std::string(node_id), 0).first;
        }
        auto& node = node_it->second;
        node += bytes;
        auto peak_it = m_peak_live_nodes.find(node_id);
        if (peak_it == m_peak_live_nodes.end()) {
            peak_it = m_peak_live_nodes.emplace(std::string(node_id), 0).first;
        }
        auto& node_peak = peak_it->second;
        node_peak = std::max(node_peak, node);

        // NodeMemoryMetrics is atomic-membered (non-copyable/non-movable);
        // pass a local by const reference (observe_node borrows it).
        NodeMemoryMetrics temporary_metric;
        temporary_metric.temporary_buffers.store(1, std::memory_order_relaxed);
        m_reporter.observe_node(node_id, temporary_metric);
        m_reporter.observe_live_bytes(node_id, node, node_peak);

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
        std::lock_guard<std::mutex> lock(m_mutex);
        subtract_saturating(m_current_live_bytes, bytes);
        auto node = m_live_nodes.find(node_id);
        if (node != m_live_nodes.end()) {
            subtract_saturating(node->second, bytes);
            auto peak_it = m_peak_live_nodes.find(node_id);
            const auto peak = peak_it != m_peak_live_nodes.end() ? peak_it->second : 0;
            m_reporter.observe_live_bytes(node_id, node->second, peak);
        }
        if (sample_key) {
            auto sample = m_samples.find(*sample_key);
            if (sample != m_samples.end()) subtract_saturating(sample->second.live_bytes, bytes);
        }
    }

    /// Fused release(old) + acquire(new) in ONE lock acquisition. Semantically
    /// identical to calling release_temporary(old) then acquire_temporary(new)
    /// (the composition execute_single_node performs per resize); the bytes in
    /// flight between the two steps are never observable because both halves
    /// of the original sequence also ran under the same session mutex.
    void transfer_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t released_bytes,
        std::uint64_t acquired_bytes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // ── release half ──
        if (released_bytes != 0) {
            subtract_saturating(m_current_live_bytes, released_bytes);
            auto node = m_live_nodes.find(node_id);
            if (node != m_live_nodes.end()) {
                subtract_saturating(node->second, released_bytes);
            }
            if (sample_key) {
                auto sample = m_samples.find(*sample_key);
                if (sample != m_samples.end()) {
                    subtract_saturating(sample->second.live_bytes, released_bytes);
                }
            }
        }

        // ── acquire half ──
        if (acquired_bytes != 0) {
            m_current_live_bytes += acquired_bytes;
            m_peak_live_bytes = std::max(m_peak_live_bytes, m_current_live_bytes);
            auto node_it = m_live_nodes.find(node_id);
            if (node_it == m_live_nodes.end()) {
                node_it = m_live_nodes.emplace(std::string(node_id), 0).first;
            }
            auto& node = node_it->second;
            node += acquired_bytes;
            auto peak_it = m_peak_live_nodes.find(node_id);
            if (peak_it == m_peak_live_nodes.end()) {
                peak_it = m_peak_live_nodes.emplace(std::string(node_id), 0).first;
            }
            auto& node_peak = peak_it->second;
            node_peak = std::max(node_peak, node);
            if (sample_key) {
                auto& sample = m_samples[*sample_key];
                sample.sample_key = *sample_key;
                sample.temporary_bytes_observed += acquired_bytes;
                sample.temporary_buffers += 1;
                sample.live_bytes += acquired_bytes;
                sample.peak_live_bytes = std::max(sample.peak_live_bytes, sample.live_bytes);
            }
        }

        if (acquired_bytes != 0 || released_bytes != 0) {
            auto node = m_live_nodes.find(node_id);
            const auto live = node != m_live_nodes.end() ? node->second : 0;
            auto peak_it = m_peak_live_nodes.find(node_id);
            const auto peak = peak_it != m_peak_live_nodes.end() ? peak_it->second : 0;
            m_reporter.observe_live_bytes(node_id, live, peak);
        }
    }

    void record_allocation(std::string_view node_id, std::uint64_t bytes) {
        NodeMemoryMetrics metrics;
        metrics.allocations.store(1, std::memory_order_relaxed);
        metrics.allocated_bytes.store(bytes, std::memory_order_relaxed);
        observe_node(node_id, metrics);
    }

    void record_pool(NodeMemoryPoolSnapshot pool) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pool = pool;
    }

    void record_rss_peak(std::uint64_t rss_bytes) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_peak_rss_bytes = std::max(m_peak_rss_bytes, rss_bytes);
    }

    /// Merge a completed isolated sample report into the owning render
    /// session. Temporal sample sessions are intentionally short-lived, so
    /// their counters must be transferred before the sample domain is
    /// destroyed; this does not merge any cache or framebuffer ownership.
    void merge_report(const NodeMemoryReport& report) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& node : report.nodes) {
            NodeMemoryMetrics metrics;
            metrics.pixels_read.store(node.pixels_read, std::memory_order_relaxed);
            metrics.pixels_written.store(node.pixels_written, std::memory_order_relaxed);
            metrics.bytes_read.store(node.bytes_read, std::memory_order_relaxed);
            metrics.bytes_written.store(node.bytes_written, std::memory_order_relaxed);
            metrics.framebuffer_copies.store(node.framebuffer_copies, std::memory_order_relaxed);
            metrics.framebuffer_clears.store(node.framebuffer_clears, std::memory_order_relaxed);
            metrics.allocations.store(node.allocations, std::memory_order_relaxed);
            metrics.allocated_bytes.store(node.allocated_bytes, std::memory_order_relaxed);
            metrics.temporary_buffers.store(node.temporary_buffers, std::memory_order_relaxed);
            m_reporter.observe_node(node.node_id, metrics);
            m_reporter.observe_live_bytes(node.node_id, node.live_bytes, node.peak_live_bytes);
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
        std::lock_guard<std::mutex> lock(m_mutex);
        NodeMemoryReport report;
        report.nodes = m_reporter.snapshot();
        report.samples.reserve(m_samples.size());
        for (const auto& [key, sample] : m_samples) report.samples.push_back(sample);
        report.framebuffer_pool = m_pool;
        report.current_live_bytes = m_current_live_bytes;
        report.peak_live_bytes = m_peak_live_bytes;
        report.peak_rss_bytes = m_peak_rss_bytes;
        return report;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reporter.reset();
        m_samples.clear();
        m_live_nodes.clear();
        m_peak_live_nodes.clear();
        m_pool = {};
        m_current_live_bytes = 0;
        m_peak_live_bytes = 0;
        m_peak_rss_bytes = 0;
    }

private:
    struct SampleKeyLess {
        bool operator()(const TemporalSampleKey& a, const TemporalSampleKey& b) const noexcept {
            if (a.frame != b.frame) return a.frame < b.frame;
            if (a.subframe_tick != b.subframe_tick) return a.subframe_tick < b.subframe_tick;
            return a.version < b.version;
        }
    };

    // Transparent comparators: find(string_view) never builds a std::string.
    // Owned keys are created exactly once, on first insertion of a node.
    struct StringLess {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a < b;
        }
    };

    static void subtract_saturating(std::uint64_t& value, std::uint64_t amount) noexcept {
        value = amount > value ? 0 : value - amount;
    }

    mutable std::mutex m_mutex;
    NodeStatsReporter m_reporter;
    std::map<std::string, std::uint64_t, StringLess> m_live_nodes;
    std::map<std::string, std::uint64_t, StringLess> m_peak_live_nodes;
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
/// OPT-TRACKER-LOOKUP: the node id is borrowed as a string_view (callers pass
/// a stack-buffer-backed view — see memory_node_id_into() in the executor);
/// the tracker owns its key strings, so the scope itself performs ZERO heap
/// allocations. Resizes through set_live_bytes() use the fused
/// transfer_temporary() path (one lock acquisition instead of two).
class ScopedNodeMemory {
public:
    ScopedNodeMemory(
        NodeMemoryTracker& tracker,
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes)
        : m_tracker(&tracker), m_node_id(node_id), m_sample_key(sample_key), m_bytes(bytes)
    {
        if (m_bytes != 0) m_tracker->acquire_temporary(m_node_id, m_sample_key, m_bytes);
    }

    ~ScopedNodeMemory() {
        if (m_tracker && m_bytes != 0) {
            m_tracker->release_temporary(m_node_id, m_sample_key, m_bytes);
        }
    }

    ScopedNodeMemory(const ScopedNodeMemory&) = delete;
    ScopedNodeMemory& operator=(const ScopedNodeMemory&) = delete;

    void set_live_bytes(std::uint64_t bytes) {
        if (!m_tracker || bytes == m_bytes) return;
        const std::uint64_t previous = m_bytes;
        m_bytes = bytes;
        if (previous == 0) {
            m_tracker->acquire_temporary(m_node_id, m_sample_key, m_bytes);
        } else if (m_bytes == 0) {
            m_tracker->release_temporary(m_node_id, m_sample_key, previous);
        } else {
            m_tracker->transfer_temporary(m_node_id, m_sample_key, previous, m_bytes);
        }
    }

    void record_allocation(std::uint64_t bytes) {
        if (m_tracker) m_tracker->record_allocation(m_node_id, bytes);
    }

private:
    NodeMemoryTracker* m_tracker;
    std::string_view m_node_id;   // borrowed: caller-owned buffer outlives the scope
    std::optional<TemporalSampleKey> m_sample_key;
    std::uint64_t m_bytes{0};
};

} // namespace chronon3d::graph
