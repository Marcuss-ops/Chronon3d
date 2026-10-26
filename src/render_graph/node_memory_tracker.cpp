#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>

namespace chronon3d::graph {

void NodeMemoryTracker::ensure_node(std::string_view node_id, std::string_view node_type) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        if (node.node_type.empty() && !node_type.empty()) {
            node.node_type.assign(node_type);
        }
    }

void NodeMemoryTracker::begin_scope(
        std::string_view node_id,
        std::string_view node_type,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        std::unique_lock<std::mutex> sample_lock;
        if (sample_key) sample_lock = std::unique_lock(m_mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        if (node.node_type.empty() && !node_type.empty()) {
            node.node_type.assign(node_type);
        }
        if (bytes == 0) return;
        node.metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);
        node.live_bytes += bytes;
        node.peak_live_bytes = std::max(node.peak_live_bytes, node.live_bytes);
        const auto current = m_current_live_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        update_peak(m_peak_live_bytes, current);
        if (sample_key) {
            auto& sample = m_samples[*sample_key];
            sample.sample_key = *sample_key;
            sample.temporary_bytes_observed += bytes;
            sample.temporary_buffers += 1;
            sample.live_bytes += bytes;
            sample.peak_live_bytes = std::max(sample.peak_live_bytes, sample.live_bytes);
        }
    }

void NodeMemoryTracker::finish_scope(
        std::string_view node_id,
        const NodeMemoryMetrics& metrics,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        std::unique_lock<std::mutex> sample_lock;
        if (sample_key) sample_lock = std::unique_lock(m_mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        add_metrics(node.metrics, metrics);
        if (bytes == 0) return;
        subtract_saturating(node.live_bytes, bytes);
        subtract_saturating(m_current_live_bytes, bytes);
        if (sample_key) {
            auto sample = m_samples.find(*sample_key);
            if (sample != m_samples.end()) {
                subtract_saturating(sample->second.live_bytes, bytes);
            }
        }
    }

void NodeMemoryTracker::resize_scope(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t old_bytes,
        std::uint64_t new_bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        std::unique_lock<std::mutex> sample_lock;
        if (sample_key) sample_lock = std::unique_lock(m_mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        if (old_bytes != 0) {
            subtract_saturating(node.live_bytes, old_bytes);
            subtract_saturating(m_current_live_bytes, old_bytes);
            if (sample_key) {
                auto sample = m_samples.find(*sample_key);
                if (sample != m_samples.end()) {
                    subtract_saturating(sample->second.live_bytes, old_bytes);
                }
            }
        }
        if (new_bytes != 0) {
            node.metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);
            node.live_bytes += new_bytes;
            node.peak_live_bytes = std::max(node.peak_live_bytes, node.live_bytes);
            const auto current = m_current_live_bytes.fetch_add(new_bytes, std::memory_order_relaxed) + new_bytes;
            update_peak(m_peak_live_bytes, current);
            if (sample_key) {
                auto& sample = m_samples[*sample_key];
                sample.sample_key = *sample_key;
                sample.temporary_bytes_observed += new_bytes;
                sample.temporary_buffers += 1;
                sample.live_bytes += new_bytes;
                sample.peak_live_bytes = std::max(sample.peak_live_bytes, sample.live_bytes);
            }
        }
    }

void NodeMemoryTracker::observe_node(std::string_view node_id, const NodeMemoryMetrics& metrics) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        // Monotonic counters fold additively; live/peak are point-in-time
        // state updated exclusively by the acquire/release path (same
        // contract NodeStatsReporter::add_into documented).
        add_metrics(node.metrics, metrics);
    }

void NodeMemoryTracker::acquire_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        std::unique_lock<std::mutex> sample_lock;
        if (sample_key) sample_lock = std::unique_lock(m_mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        node.metrics.temporary_buffers.fetch_add(1, std::memory_order_relaxed);

        node.live_bytes += bytes;
        node.peak_live_bytes = std::max(node.peak_live_bytes, node.live_bytes);

        const auto current = m_current_live_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
        update_peak(m_peak_live_bytes, current);

        if (sample_key) {
            auto& sample = m_samples[*sample_key];
            sample.sample_key = *sample_key;
            sample.temporary_bytes_observed += bytes;
            sample.temporary_buffers += 1;
            sample.live_bytes += bytes;
            sample.peak_live_bytes = std::max(sample.peak_live_bytes, sample.live_bytes);
        }
    }

void NodeMemoryTracker::release_temporary(
        std::string_view node_id,
        std::optional<TemporalSampleKey> sample_key,
        std::uint64_t bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        std::unique_lock<std::mutex> sample_lock;
        if (sample_key) sample_lock = std::unique_lock(m_mutex);
        // Every release is paired with an acquire that registered the id;
        // find_or_emplace keeps an unpaired release safe rather than UB.
        NodeLiveState& node = find_or_emplace(shard, node_id);
        subtract_saturating(node.live_bytes, bytes);
        subtract_saturating(m_current_live_bytes, bytes);
        if (sample_key) {
            auto sample = m_samples.find(*sample_key);
            if (sample != m_samples.end()) {
                subtract_saturating(sample->second.live_bytes, bytes);
            }
        }
    }

void NodeMemoryTracker::record_allocation(std::string_view node_id, std::uint64_t bytes) {
        auto& shard = node_shard(node_id);
        const MeteredLock lock(shard.mutex);
        NodeLiveState& node = find_or_emplace(shard, node_id);
        node.metrics.allocations.fetch_add(1, std::memory_order_relaxed);
        node.metrics.allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }

void NodeMemoryTracker::record_pool(NodeMemoryPoolSnapshot pool) {
        const MeteredLock lock(m_metadata_mutex);
        m_pool = pool;
    }

void NodeMemoryTracker::record_rss_peak(std::uint64_t rss_bytes) {
        const MeteredLock lock(m_metadata_mutex);
        m_peak_rss_bytes = std::max(m_peak_rss_bytes, rss_bytes);
    }

void NodeMemoryTracker::merge_report(const NodeMemoryReport& report) {
        for (const auto& node : report.nodes) {
            auto& shard = node_shard(node.node_id);
            const MeteredLock lock(shard.mutex);
            NodeLiveState& dst = find_or_emplace(shard, node.node_id);
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
        update_peak(m_current_live_bytes, report.current_live_bytes);
        update_peak(m_peak_live_bytes, report.peak_live_bytes);
        {
            std::lock_guard lock(m_mutex);
            for (const auto& sample : report.samples) {
                auto& dst = m_samples[sample.sample_key];
                dst.sample_key = sample.sample_key;
                dst.temporary_bytes_observed += sample.temporary_bytes_observed;
                dst.temporary_buffers += sample.temporary_buffers;
                dst.live_bytes = sample.live_bytes;
                dst.peak_live_bytes = std::max(dst.peak_live_bytes, sample.peak_live_bytes);
            }
        }
        {
            const MeteredLock lock(m_metadata_mutex);
            m_pool.current_bytes = std::max(m_pool.current_bytes, report.framebuffer_pool.current_bytes);
            m_pool.retained_bytes = std::max(m_pool.retained_bytes, report.framebuffer_pool.retained_bytes);
            m_pool.peak_retained_bytes = std::max(m_pool.peak_retained_bytes, report.framebuffer_pool.peak_retained_bytes);
            m_pool.total_allocations += report.framebuffer_pool.total_allocations;
            m_pool.total_reuses += report.framebuffer_pool.total_reuses;
            m_pool.total_returns += report.framebuffer_pool.total_returns;
            m_pool.evicted_bytes += report.framebuffer_pool.evicted_bytes;
            m_peak_rss_bytes = std::max(m_peak_rss_bytes, report.peak_rss_bytes);
        }
    }

[[nodiscard]] NodeMemoryReport NodeMemoryTracker::snapshot() const {
        std::array<std::unique_lock<std::mutex>, kShardCount> shard_locks;
        for (std::size_t i = 0; i < kShardCount; ++i) {
            shard_locks[i] = std::unique_lock(m_node_shards[i].mutex);
        }
        std::lock_guard sample_lock(m_mutex);
        NodeMemoryReport report;
        std::size_t node_count = 0;
        for (const auto& shard : m_node_shards) node_count += shard.nodes.size();
        report.nodes.reserve(node_count);
        for (const auto& shard : m_node_shards) {
            for (const auto& [node_id, node] : shard.nodes) {
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
        }
        std::sort(report.nodes.begin(), report.nodes.end(), [](const auto& a, const auto& b) {
            return a.node_id < b.node_id;
        });
        report.samples.reserve(m_samples.size());
        for (const auto& [key, sample] : m_samples) report.samples.push_back(sample);
        report.framebuffer_pool = m_pool;
        report.current_live_bytes = m_current_live_bytes.load(std::memory_order_relaxed);
        report.peak_live_bytes = m_peak_live_bytes.load(std::memory_order_relaxed);
        report.peak_rss_bytes = m_peak_rss_bytes;
        return report;
    }

void NodeMemoryTracker::reset() {
        std::array<std::unique_lock<std::mutex>, kShardCount> shard_locks;
        for (std::size_t i = 0; i < kShardCount; ++i) {
            shard_locks[i] = std::unique_lock(m_node_shards[i].mutex);
        }
        std::lock_guard sample_lock(m_mutex);
        for (auto& shard : m_node_shards) shard.nodes.clear();
        m_samples.clear();
        m_pool = {};
        m_current_live_bytes.store(0, std::memory_order_relaxed);
        m_peak_live_bytes.store(0, std::memory_order_relaxed);
        m_peak_rss_bytes = 0;
    }

} // namespace chronon3d::graph
