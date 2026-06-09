#pragma once
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace chronon3d::telemetry {

namespace detail {

/// Round-robin sharded store that eliminates the single-mutex bottleneck
/// of the previous implementation.  Each `record()` call picks a different
/// shard via an atomic counter, so concurrent recordings from different
/// threads (or the same thread) almost never contend.  `collect()` and
/// `clear()` briefly lock all 16 shards to drain the data.
template <typename Record>
class ShardedTelemetryStore {
public:
    static constexpr std::size_t kShards = 16;

    void record(const Record& r) {
        const std::size_t idx =
            m_next_shard.fetch_add(1, std::memory_order_relaxed) % kShards;
        std::lock_guard<std::mutex> lock(m_mutexes[idx]);
        m_stores[idx].push_back(r);
    }

    std::vector<Record> collect() {
        std::vector<Record> result;
        for (std::size_t i = 0; i < kShards; ++i) {
            std::lock_guard<std::mutex> lock(m_mutexes[i]);
            auto& store = m_stores[i];
            result.insert(
                result.end(),
                std::make_move_iterator(store.begin()),
                std::make_move_iterator(store.end()));
            store.clear();
        }
        return result;
    }

    void clear() {
        for (std::size_t i = 0; i < kShards; ++i) {
            std::lock_guard<std::mutex> lock(m_mutexes[i]);
            m_stores[i].clear();
        }
    }

private:
    std::array<std::mutex, kShards> m_mutexes;
    std::array<std::vector<Record>, kShards> m_stores;
    std::atomic<std::size_t> m_next_shard{0};
};

} // namespace detail

// One sharded store per telemetry category.  The old design had 14
// independent `static std::mutex` objects (one per category) which meant
// every recorder call acquired a process-wide mutex.  With sharding,
// concurrent recordings distribute across 16 internal mutexes per category.

inline detail::ShardedTelemetryStore<NodeTelemetryRecord>& node_telemetry_store() {
    static detail::ShardedTelemetryStore<NodeTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<LayerTelemetryRecord>& layer_telemetry_store() {
    static detail::ShardedTelemetryStore<LayerTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<CacheTelemetryRecord>& cache_telemetry_store() {
    static detail::ShardedTelemetryStore<CacheTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<CullingTelemetryRecord>& culling_telemetry_store() {
    static detail::ShardedTelemetryStore<CullingTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<TextTelemetryRecord>& text_telemetry_store() {
    static detail::ShardedTelemetryStore<TextTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<ImageTelemetryRecord>& image_telemetry_store() {
    static detail::ShardedTelemetryStore<ImageTelemetryRecord> store;
    return store;
}

inline detail::ShardedTelemetryStore<TileTelemetryRecord>& tile_telemetry_store() {
    static detail::ShardedTelemetryStore<TileTelemetryRecord> store;
    return store;
}

// ── Node telemetry ──────────────────────────────────────────────────
inline void record_node_telemetry(const NodeTelemetryRecord& rec) {
    node_telemetry_store().record(rec);
}
inline std::vector<NodeTelemetryRecord> collect_node_telemetry() {
    return node_telemetry_store().collect();
}

// ── Layer telemetry ─────────────────────────────────────────────────
inline void record_layer_telemetry(const LayerTelemetryRecord& rec) {
    layer_telemetry_store().record(rec);
}
inline std::vector<LayerTelemetryRecord> collect_layer_telemetry() {
    return layer_telemetry_store().collect();
}

// ── Cache telemetry ─────────────────────────────────────────────────
inline void record_cache_telemetry(const CacheTelemetryRecord& rec) {
    cache_telemetry_store().record(rec);
}
inline std::vector<CacheTelemetryRecord> collect_cache_telemetry() {
    return cache_telemetry_store().collect();
}

// ── Culling telemetry ───────────────────────────────────────────────
inline void record_culling_telemetry(const CullingTelemetryRecord& rec) {
    culling_telemetry_store().record(rec);
}
inline std::vector<CullingTelemetryRecord> collect_culling_telemetry() {
    return culling_telemetry_store().collect();
}

// ── Text telemetry ──────────────────────────────────────────────────
inline void record_text_telemetry(const TextTelemetryRecord& rec) {
    text_telemetry_store().record(rec);
}
inline std::vector<TextTelemetryRecord> collect_text_telemetry() {
    return text_telemetry_store().collect();
}

// ── Image telemetry ─────────────────────────────────────────────────
inline void record_image_telemetry(const ImageTelemetryRecord& rec) {
    image_telemetry_store().record(rec);
}
inline std::vector<ImageTelemetryRecord> collect_image_telemetry() {
    return image_telemetry_store().collect();
}

// ── Tile telemetry ──────────────────────────────────────────────────
inline void record_tile_telemetry(const TileTelemetryRecord& rec) {
    tile_telemetry_store().record(rec);
}
inline std::vector<TileTelemetryRecord> collect_tile_telemetry() {
    return tile_telemetry_store().collect();
}

/// Clear all in-memory telemetry event stores.
/// Call this after warmup to prevent warmup events from appearing
/// alongside render-loop events (which would contradict atomic counters
/// that were reset after warmup).
inline void clear_telemetry_stores() {
    node_telemetry_store().clear();
    layer_telemetry_store().clear();
    cache_telemetry_store().clear();
    culling_telemetry_store().clear();
    text_telemetry_store().clear();
    image_telemetry_store().clear();
    tile_telemetry_store().clear();
}

} // namespace chronon3d::telemetry
