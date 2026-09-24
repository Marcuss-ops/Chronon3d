#pragma once

// ============================================================================
// Canonical per-node memory metrics contract.
//
// NodeMemoryMetrics and NodeStatsReporter are intentionally small public
// value/reporting types. Runtime-only RAII tracking lives in
// include/chronon3d/internal/render_graph/node_memory_tracker.hpp so the SDK header does
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
    /// Number of framebuffer backing-store allocation events observed while
    /// this node is executing, plus explicit record_allocation() events.
    std::atomic<std::uint64_t> allocations{0};
    /// Sum of framebuffer backing-store bytes and explicit allocation bytes.
    std::atomic<std::uint64_t> allocated_bytes{0};
    /// Number of temporary buffers acquired, independent of heap allocations.
    std::atomic<std::uint64_t> temporary_buffers{0};
    /// Current live bytes held by this node's temporary workspaces.
    std::atomic<std::uint64_t> live_bytes{0};
    /// High-water mark of live bytes held by this node.
    std::atomic<std::uint64_t> peak_live_bytes{0};

    NodeMemoryMetrics() = default;
    NodeMemoryMetrics(const NodeMemoryMetrics& other) noexcept { copy_from(other); }
    NodeMemoryMetrics& operator=(const NodeMemoryMetrics& other) noexcept {
        if (this != &other) copy_from(other);
        return *this;
    }

private:
    void copy_from(const NodeMemoryMetrics& other) noexcept {
        pixels_read.store(other.pixels_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        pixels_written.store(other.pixels_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        bytes_read.store(other.bytes_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        bytes_written.store(other.bytes_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_copies.store(other.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_clears.store(other.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
        allocations.store(other.allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
        allocated_bytes.store(other.allocated_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        temporary_buffers.store(other.temporary_buffers.load(std::memory_order_relaxed), std::memory_order_relaxed);
        live_bytes.store(other.live_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        peak_live_bytes.store(other.peak_live_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
};

/// Value snapshot of one node's accumulated counters.
struct NodeStatsSnapshot {
    std::string node_id{};
    std::string node_type{};
    std::uint64_t pixels_read{0};
    std::uint64_t pixels_written{0};
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
    std::uint64_t framebuffer_copies{0};
    std::uint64_t framebuffer_clears{0};
    std::uint64_t allocations{0};
    std::uint64_t allocated_bytes{0};
    std::uint64_t temporary_buffers{0};
    std::uint64_t live_bytes{0};
    std::uint64_t peak_live_bytes{0};
};

/// Per-session, thread-safe monotonic reporter.
///
/// OPT-TRACKER-LOOKUP: `node_id` is accepted by value `std::string` (callers
/// with an owned id) OR by `std::string_view` via the heterogeneous overloads
/// below — hot-path callers pass views so observations never allocate.
class NodeStatsReporter {
public:
    NodeStatsReporter() = default;
    NodeStatsReporter(const NodeStatsReporter&) = delete;
    NodeStatsReporter& operator=(const NodeStatsReporter&) = delete;

    /// Heterogeneous observation — borrows `node_id` (no allocation).
    /// An owned key is created only when the id is seen for the first time.
    /// NOTE: single string_view overload (no `const std::string&` sibling) so
    /// string-literal call sites resolve unambiguously; owned std::string
    /// arguments implicitly convert to the view.
    void observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_acc.find(node_id);
        if (it == m_acc.end()) {
            auto fresh = std::make_unique<NodeMemoryMetrics>();
            add_into(*fresh, metrics);
            m_acc.emplace(std::string(node_id), std::move(fresh));
            return;
        }
        add_into(*it->second, metrics);
    }

    /// Heterogeneous live-bytes observation — borrows `node_id` (single
    /// string_view overload; see observe_node note).
    void observe_live_bytes(
        std::string_view node_id,
        std::uint64_t live_bytes,
        std::uint64_t peak_live_bytes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_acc.find(node_id);
        if (it == m_acc.end()) {
            auto fresh = std::make_unique<NodeMemoryMetrics>();
            fresh->live_bytes.store(live_bytes, std::memory_order_relaxed);
            fresh->peak_live_bytes.store(peak_live_bytes, std::memory_order_relaxed);
            m_acc.emplace(std::string(node_id), std::move(fresh));
            return;
        }
        it->second->live_bytes.store(live_bytes, std::memory_order_relaxed);
        auto observed_peak = it->second->peak_live_bytes.load(std::memory_order_relaxed);
        while (observed_peak < peak_live_bytes &&
               !it->second->peak_live_bytes.compare_exchange_weak(
                   observed_peak, peak_live_bytes,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
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
                .live_bytes = metrics.live_bytes.load(std::memory_order_relaxed),
                .peak_live_bytes = metrics.peak_live_bytes.load(std::memory_order_relaxed),
            });
        }
        return out;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_acc.clear();
    }

private:
    // Transparent comparator: find(string_view) never builds a std::string.
    struct StringLess {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a < b;
        }
    };

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
        // live_bytes and peak_live_bytes are point-in-time state, not
        // monotonic deltas. They are updated exclusively by
        // observe_live_bytes() so allocation observations cannot overwrite
        // the current workspace state.
    }

    mutable std::mutex m_mutex;
    std::map<std::string, std::unique_ptr<NodeMemoryMetrics>, StringLess> m_acc;
};

} // namespace chronon3d::graph
