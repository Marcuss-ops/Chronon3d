#pragma once

#include <chronon3d/core/profiling/profiling_context.hpp>  // g_current_counters
#include <chronon3d/core/profiling/render_counter_types.hpp>  // RenderCounters
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/render_graph/executor/node_memory_metrics.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::graph {

/// Inline identity used by the executor on every node/frame. Canonical IDs
/// fit in the fixed buffer; only non-canonical long fallback names use heap.
class NodeMemoryId {
public:
    static constexpr std::size_t kInlineCapacity = 64;

    NodeMemoryId() = default;
    explicit NodeMemoryId(std::string_view value) { assign(value); }

    [[nodiscard]] char* data() noexcept { return inline_.data(); }
    void set_size(std::size_t size) noexcept { size_ = size; }
    [[nodiscard]] std::string_view view() const noexcept {
        return overflow_.empty()
            ? std::string_view{inline_.data(), size_}
            : std::string_view{overflow_};
    }

private:
    void assign(std::string_view value) {
        if (value.size() <= inline_.size()) {
            std::copy(value.begin(), value.end(), inline_.begin());
            size_ = value.size();
            return;
        }
        overflow_.assign(value);
    }

    std::array<char, kInlineCapacity> inline_{};
    std::size_t size_{0};
    std::string overflow_;
};

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
/// concurrent graph executions update a key-selected node shard, while
/// TemporalSampleKey keeps sample domains distinct. The owning session may
/// call reset() at an explicit report-lifetime boundary.
///
/// HOT-PATH TAX (TICKET-HOTPATH-ZERO-ALLOC-P1) — folded state and batched
/// scope accounting and sharding. Node counters and live state share one
/// heterogeneous table per key-hashed shard. A normal ScopedNodeMemory scope
/// takes one shard lock at entry and one at exit; registration + acquire are
/// combined in begin_scope(), and metric fold + release in finish_scope(). A
/// temporal sample additionally takes the sample-domain mutex. Explicit
/// tracker operations retain their standalone APIs for non-RAII callers.
/// Canonical IDs remain allocation free in NodeMemoryId; snapshots and report
/// merges are cold-path operations that lock shards in index order.
class NodeMemoryTracker {
public:
    static constexpr std::size_t kShardCount = 16;
    NodeMemoryTracker() = default;
    NodeMemoryTracker(const NodeMemoryTracker&) = delete;
    NodeMemoryTracker& operator=(const NodeMemoryTracker&) = delete;

    /// Reserve the per-node aggregate before the execution scope enters its
    /// noexcept destructor path. Subsequent observations for this key update
    /// the existing row without allocating.
void ensure_node(std::string_view node_id, std::string_view node_type = {});

    /// Register a node execution and its temporary bytes in one critical
    /// section. ScopedNodeMemory uses this instead of a separate ensure/acquire
    /// pair on every frame.
void begin_scope(
        std::string_view node_id,
        std::string_view node_type,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes);

    /// Fold node counters and release its temporary bytes under one lock.
void finish_scope(
        std::string_view node_id,
        const NodeMemoryMetrics& metrics,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes);

    /// Apply a scoped live-byte resize atomically with respect to snapshots.
void resize_scope(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t old_bytes,
        std::uint64_t new_bytes);

void observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics);

void acquire_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes);

void release_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes);

void record_allocation(std::string_view node_id, std::uint64_t bytes);

void record_pool(NodeMemoryPoolSnapshot pool);

void record_rss_peak(std::uint64_t rss_bytes);

    /// Merge a completed isolated sample report into the owning render
    /// session. Temporal sample sessions are intentionally short-lived, so
    /// their counters must be transferred before the sample domain is
    /// destroyed; this does not merge any cache or framebuffer ownership.
void merge_report(const NodeMemoryReport& report);

[[nodiscard]] NodeMemoryReport snapshot() const;

void reset();

private:
    static void add_metrics(
        NodeMemoryMetrics& destination, const NodeMemoryMetrics& source) noexcept {
        destination.pixels_read.fetch_add(source.pixels_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.pixels_written.fetch_add(source.pixels_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.bytes_read.fetch_add(source.bytes_read.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.bytes_written.fetch_add(source.bytes_written.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.framebuffer_copies.fetch_add(source.framebuffer_copies.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.framebuffer_clears.fetch_add(source.framebuffer_clears.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.allocations.fetch_add(source.allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.allocated_bytes.fetch_add(source.allocated_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        destination.temporary_buffers.fetch_add(source.temporary_buffers.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    /// Lock metering shared by the node shards and cold-path metadata locks.
    ///
    /// The tracker is attached to every session, so each node × frame still
    /// enters its node shard twice (scope entry and exit). Distinct node keys
    /// progress independently; repeated execution of one key remains ordered.
    /// The P1 decision ("restructure, or leave it alone") must
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
            : mutex_(mutex), counters_(profiling::g_current_counters) {
            if (counters_ != nullptr) {
                wait_t0_ = std::chrono::steady_clock::now();
            }
            mutex_.lock();
            if (counters_ == nullptr) return;
            hold_t0_ = std::chrono::steady_clock::now();
            const auto wait_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    hold_t0_ - wait_t0_).count());
            counters_->node_memory_lock_acquires.fetch_add(
                1, std::memory_order_relaxed);
            counters_->node_memory_lock_wait_us.fetch_add(
                wait_us, std::memory_order_relaxed);
            record_max(counters_->node_memory_lock_max_wait_us, wait_us);
        }

        ~MeteredLock() {
            mutex_.unlock();
            if (counters_ == nullptr) return;
            counters_->node_memory_lock_hold_wall_us.fetch_add(
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
        decltype(profiling::g_current_counters) counters_{nullptr};
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

    struct NodeShard {
        mutable std::mutex mutex;
        std::map<std::string, std::unique_ptr<NodeLiveState>, StringViewLess> nodes;
    };

    [[nodiscard]] NodeShard& node_shard(std::string_view node_id) noexcept {
        return m_node_shards[std::hash<std::string_view>{}(node_id) % kShardCount];
    }

    /// Find or insert in the node shard. Caller holds that shard's mutex.
    [[nodiscard]] NodeLiveState& find_or_emplace(
        NodeShard& shard, std::string_view node_id) {
        auto it = shard.nodes.find(node_id);
        if (it != shard.nodes.end()) {
            return *it->second;
        }
        auto fresh = std::make_unique<NodeLiveState>();
        NodeLiveState& ref = *fresh;
        shard.nodes.emplace(std::string(node_id), std::move(fresh));
        return ref;
    }

    static void subtract_saturating(std::uint64_t& value, std::uint64_t amount) noexcept {
        value = amount > value ? 0 : value - amount;
    }

    static void subtract_saturating(
        std::atomic<std::uint64_t>& value, std::uint64_t amount) noexcept {
        auto current = value.load(std::memory_order_relaxed);
        while (!value.compare_exchange_weak(
            current, amount > current ? 0 : current - amount,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    static void update_peak(
        std::atomic<std::uint64_t>& peak, std::uint64_t value) noexcept {
        auto current = peak.load(std::memory_order_relaxed);
        while (current < value && !peak.compare_exchange_weak(
            current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    mutable std::mutex m_mutex;
    mutable std::mutex m_metadata_mutex;
    std::array<NodeShard, kShardCount> m_node_shards{};
    std::map<TemporalSampleKey, NodeMemorySampleSnapshot, SampleKeyLess> m_samples;
    NodeMemoryPoolSnapshot m_pool{};
    std::atomic<std::uint64_t> m_current_live_bytes{0};
    std::atomic<std::uint64_t> m_peak_live_bytes{0};
    std::uint64_t m_peak_rss_bytes{0};
};

/// RAII live-memory scope. It records a separate allocation event only when
/// record_allocation() is explicitly requested, so borrowed/reused buffers do
/// not inflate allocation counters.
///
/// The node id is stored inline because the canonical executor generates a
/// temporary identity for each node/frame. A borrowed view would dangle;
/// retaining that identity in this fixed buffer also avoids heap churn.
class ScopedNodeMemory {
public:
    ScopedNodeMemory(
        NodeMemoryTracker& tracker,
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes,
        std::string_view node_type = {})
        : ScopedNodeMemory(
              tracker, NodeMemoryId(node_id), sample_key, bytes, node_type) {}

    ScopedNodeMemory(
        NodeMemoryTracker& tracker,
        NodeMemoryId node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes,
        std::string_view node_type = {})
        : m_tracker(&tracker), m_node_id(std::move(node_id)), m_sample_key(sample_key), m_bytes(bytes)
    {
        m_tracker->begin_scope(m_node_id.view(), node_type, m_sample_key, m_bytes);
        m_previous_sink = profiling::g_node_memory_event_sink;
        m_event_sink = profiling::NodeMemoryEventSink{
            .context = this,
            .record = &ScopedNodeMemory::record_event,
        };
        profiling::g_node_memory_event_sink = &m_event_sink;
    }

    ~ScopedNodeMemory() {
        profiling::g_node_memory_event_sink = m_previous_sink;
        if (m_tracker) {
            m_tracker->finish_scope(m_node_id.view(), m_metrics, m_sample_key, m_bytes);
        }
    }

    ScopedNodeMemory(const ScopedNodeMemory&) = delete;
    ScopedNodeMemory& operator=(const ScopedNodeMemory&) = delete;

    void set_live_bytes(std::uint64_t bytes) {
        if (!m_tracker || bytes == m_bytes) return;
        m_tracker->resize_scope(m_node_id.view(), m_sample_key, m_bytes, bytes);
        m_bytes = bytes;
        if (m_bytes != 0) m_metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);
    }

    void record_allocation(std::uint64_t bytes) {
        if (m_tracker) m_tracker->record_allocation(m_node_id.view(), bytes);
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
    NodeMemoryId m_node_id;
    std::optional<TemporalSampleKey> m_sample_key;
    std::uint64_t m_bytes{0};
    NodeMemoryMetrics m_metrics{};
    profiling::NodeMemoryEventSink m_event_sink{};
    profiling::NodeMemoryEventSink* m_previous_sink{nullptr};
};

} // namespace chronon3d::graph
