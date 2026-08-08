#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/render_graph/executor/node_memory_metrics.hpp>

#include <algorithm>
#include <cstdint>
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
class NodeMemoryTracker {
public:
    NodeMemoryTracker() = default;
    NodeMemoryTracker(const NodeMemoryTracker&) = delete;
    NodeMemoryTracker& operator=(const NodeMemoryTracker&) = delete;

    void observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_reporter.observe_node(std::string(node_id), metrics);
    }

    void acquire_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_live_bytes += bytes;
        m_peak_live_bytes = std::max(m_peak_live_bytes, m_current_live_bytes);
        auto& node = m_live_nodes[std::string(node_id)];
        node += bytes;
        auto& node_peak = m_peak_live_nodes[std::string(node_id)];
        node_peak = std::max(node_peak, node);

        NodeMemoryMetrics metrics;
        metrics.temporary_buffers.store(1, std::memory_order_relaxed);
        m_reporter.observe_node(std::string(node_id), metrics);
        m_reporter.observe_live_bytes(std::string(node_id), node, node_peak);

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
        auto node = m_live_nodes.find(std::string(node_id));
        if (node != m_live_nodes.end()) {
            subtract_saturating(node->second, bytes);
            const auto peak = m_peak_live_nodes[std::string(node_id)];
            m_reporter.observe_live_bytes(std::string(node_id), node->second, peak);
        }
        if (sample_key) {
            auto sample = m_samples.find(*sample_key);
            if (sample != m_samples.end()) subtract_saturating(sample->second.live_bytes, bytes);
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

    static void subtract_saturating(std::uint64_t& value, std::uint64_t amount) noexcept {
        value = amount > value ? 0 : value - amount;
    }

    mutable std::mutex m_mutex;
    NodeStatsReporter m_reporter;
    std::map<std::string, std::uint64_t> m_live_nodes;
    std::map<std::string, std::uint64_t> m_peak_live_nodes;
    std::map<TemporalSampleKey, NodeMemorySampleSnapshot, SampleKeyLess> m_samples;
    NodeMemoryPoolSnapshot m_pool{};
    std::uint64_t m_current_live_bytes{0};
    std::uint64_t m_peak_live_bytes{0};
    std::uint64_t m_peak_rss_bytes{0};
};

/// RAII live-memory scope. It records a separate allocation event only when
/// record_allocation() is explicitly requested, so borrowed/reused buffers do
/// not inflate allocation counters.
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
        if (m_bytes != 0) m_tracker->release_temporary(m_node_id, m_sample_key, m_bytes);
        m_bytes = bytes;
        if (m_bytes != 0) m_tracker->acquire_temporary(m_node_id, m_sample_key, m_bytes);
    }

    void record_allocation(std::uint64_t bytes) {
        if (m_tracker) m_tracker->record_allocation(m_node_id, bytes);
    }

private:
    NodeMemoryTracker* m_tracker;
    std::string m_node_id;
    std::optional<TemporalSampleKey> m_sample_key;
    std::uint64_t m_bytes{0};
};

} // namespace chronon3d::graph
