#pragma once

// ============================================================================
// Canonical per-node memory metrics contract.
//
// NodeMemoryMetrics and NodeStatsReporter are intentionally small public
// value/reporting types. Runtime-only RAII tracking lives in
// src/render_graph/executor/node_memory_tracker.hpp so the SDK header does
// not expose renderer-internal lifetime machinery.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chronon3d::graph {

/// Monotonic counters accumulated for one render-graph node.
struct NodeMemoryMetrics {
    std::atomic<std::uint64_t> pixels_read{0};
    std::atomic<std::uint64_t> pixels_written{0};
    std::atomic<std::uint64_t> bytes_read{0};
    std::atomic<std::uint64_t> bytes_written{0};
    std::atomic<std::uint64_t> framebuffer_copies{0};
    std::atomic<std::uint64_t> framebuffer_clears{0};
    /// Number of distinct heap allocation events observed for this node.
    std::atomic<std::uint64_t> allocations{0};
    /// Sum of bytes associated with those allocation events.
    std::atomic<std::uint64_t> allocated_bytes{0};
    std::atomic<std::uint64_t> temporary_buffers{0};
};

/// Value snapshot of one node's accumulated counters.
struct NodeStatsSnapshot {
    std::string node_id{};
    std::uint64_t pixels_read{0};
    std::uint64_t pixels_written{0};
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
    std::uint64_t framebuffer_copies{0};
    std::uint64_t framebuffer_clears{0};
    std::uint64_t allocations{0};
    std::uint64_t allocated_bytes{0};
    std::uint64_t temporary_buffers{0};
};

/// Per-session, thread-safe monotonic reporter.
class NodeStatsReporter {
public:
    NodeStatsReporter() = default;
    NodeStatsReporter(const NodeStatsReporter&) = delete;
    NodeStatsReporter& operator=(const NodeStatsReporter&) = delete;

    void observe_node(const std::string& node_id, const NodeMemoryMetrics& metrics) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_acc.find(node_id);
        if (it == m_acc.end()) {
            auto fresh = std::make_unique<NodeMemoryMetrics>();
            add_into(*fresh, metrics);
            m_acc.emplace(node_id, std::move(fresh));
            return;
        }
        add_into(*it->second, metrics);
    }

    [[nodiscard]] std::vector<NodeStatsSnapshot> snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<NodeStatsSnapshot> out;
        out.reserve(m_acc.size());
        for (const auto& [node_id, metrics_ptr] : m_acc) {
            const auto& metrics = *metrics_ptr;
            out.push_back(NodeStatsSnapshot{
                .node_id = node_id,
                .pixels_read = metrics.pixels_read.load(std::memory_order_relaxed),
                .pixels_written = metrics.pixels_written.load(std::memory_order_relaxed),
                .bytes_read = metrics.bytes_read.load(std::memory_order_relaxed),
                .bytes_written = metrics.bytes_written.load(std::memory_order_relaxed),
                .framebuffer_copies = metrics.framebuffer_copies.load(std::memory_order_relaxed),
                .framebuffer_clears = metrics.framebuffer_clears.load(std::memory_order_relaxed),
                .allocations = metrics.allocations.load(std::memory_order_relaxed),
                .allocated_bytes = metrics.allocated_bytes.load(std::memory_order_relaxed),
                .temporary_buffers = metrics.temporary_buffers.load(std::memory_order_relaxed),
            });
        }
        return out;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_acc.clear();
    }

private:
    static void add_into(NodeMemoryMetrics& dst, const NodeMemoryMetrics& src) {
        dst.pixels_read.fetch_add(src.pixels_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.pixels_written.fetch_add(src.pixels_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.bytes_read.fetch_add(src.bytes_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.bytes_written.fetch_add(src.bytes_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.framebuffer_copies.fetch_add(src.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.framebuffer_clears.fetch_add(src.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.allocations.fetch_add(src.allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.allocated_bytes.fetch_add(src.allocated_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dst.temporary_buffers.fetch_add(src.temporary_buffers.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    mutable std::mutex m_mutex;
    std::map<std::string, std::unique_ptr<NodeMemoryMetrics>> m_acc;
};

} // namespace chronon3d::graph
