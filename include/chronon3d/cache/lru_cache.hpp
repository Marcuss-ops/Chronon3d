#pragma once

#include <atomic>
#include <cstdio>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
namespace chronon3d::cache::detail {
void log_item_too_large(size_t weight, size_t capacity_weight, const char* context);
} // namespace chronon3d::cache::detail

namespace chronon3d::cache {

/**
 * @brief Capacity measurement strategy for LruCache.
 *
 * `Weight` (default): capacity is the sum of caller-supplied entry weights
 * (typically computed from byte size or per-entry cost).
 *
 * `Count`: capacity is the number of entries; every `put` contributes weight
 * 1 regardless of the per-call weight argument.  Useful for caches with
 * bounded cardinality (SceneProgramCache, CacheRegistry entries).
 *
 * In Count mode the `weight` parameter of `put()` / `compute_if_absent()` is
 * recorded as 1 in shard bookkeeping, but the caller-supplied value is still
 * observed for the "oversized item" guard (which becomes "count > 1" — i.e.
 * never oversized unless the caller explicitly passes weight > capacity).
 */
enum class CapacityMode {
    Weight,
    Count,
};

/**
 * @brief A generic, thread-safe LRU cache with sharding to reduce mutex contention.
 */
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    /// Callback invoked under the shard's mutex for every LRU eviction.
    /// Receives the key and a const ref to the value being evicted so
    /// callers (e.g. SceneProgramCache) can perform per-entry cleanup like
    /// telemetry increments, precomp invalidation, or external invalidation.
    ///
    /// WARNING: must NOT re-enter THIS cache (put/get/erase/clear/resize) from
    /// inside the callback — that would re-acquire the shard's mutex and
    /// deadlock immediately, or hit a lock-ordering hazard across shards.
    /// External side-effects (logging, telemetry counter increments, calling
    /// other caches that do not re-enter THIS one) are safe.
    using EvictCallback = std::function<void(const Key&, const Value&)>;

    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t current_size{0};
        size_t current_weight{0};
    };

    /// Construct a cache with `capacity_weight` units per shard split across
    /// `num_shards` shards.  In Count mode every entry contributes weight 1
    /// regardless of the per-call weight.  `on_evict` (optional) fires for
    /// every LRU eviction while holding the shard's mutex.
    explicit LruCache(
        size_t capacity_weight,
        size_t num_shards = 2,
        CapacityMode mode = CapacityMode::Weight,
        EvictCallback on_evict = {})
        : m_mode(mode)
        , m_on_evict(std::move(on_evict))
        , m_shards(num_shards)
    {
        size_t shard_capacity = capacity_weight / num_shards;
        if (shard_capacity == 0) shard_capacity = 1;
        for (auto& shard : m_shards) {
            shard = std::make_unique<Shard>(shard_capacity);
        }
    }

    /// Read-only accessor for the current capacity mode (testing / diag).
    [[nodiscard]] CapacityMode capacity_mode() const noexcept { return m_mode; }

    std::optional<Value> get(const Key& key) {
        auto& shard = get_shard(key);
        auto val = shard.get(key);
        if (val) {
            m_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_misses.fetch_add(1, std::memory_order_relaxed);
        }
        return val;
    }

    template <typename Func>
    void for_each(Func&& func) const {
        for (const auto& shard : m_shards) {
            std::lock_guard lock(shard->mutex);
            for (const auto& [k, entry] : shard->entries) {
                func(k, entry.value, Hash{}(k));
            }
        }
    }

    void put(const Key& key, Value value, size_t weight = 1) {
        const size_t effective_weight =
            (m_mode == CapacityMode::Count) ? size_t{1} : weight;
        get_shard(key).put(key, std::move(value), effective_weight, m_evictions, m_on_evict);
    }

    /// Atomically compute a value on cache miss while holding the per-shard lock.
    /// This avoids the classic double-checked-locking pattern and eliminates
    /// the need for an external mutex in callers (e.g. ImageCache).
    ///
    /// Semantics:
    ///   - If the key is present in the cache, the cached value is returned
    ///     and the loader is NOT called. The entry is moved to the front of
    ///     the LRU list.
    ///   - If the key is absent, the loader is invoked WHILE HOLDING the
    ///     shard's mutex. This guarantees that only one thread per shard
    ///     computes a given key at a time (preventing duplicate loads).
    ///     Other shards proceed in parallel.
    ///   - The loader must return std::pair<Value, size_t> (value, weight).
    ///   - In `CapacityMode::Count` the returned weight field is silently
    ///     ignored; every entry contributes exactly 1 unit to shard capacity
    ///     regardless of what the loader returns.  Callers should pass
    ///     `weight=1` in Count mode to document intent.
    ///   - If the returned (or effective) weight exceeds the shard capacity,
    ///     the value is returned but NOT cached (a warning is logged).
    ///   - If the loader throws, no entry is inserted and the exception
    ///     propagates to the caller.
    template <typename Func>
    Value compute_if_absent(const Key& key, Func&& loader) {
        static_assert(
            std::is_invocable_r_v<std::pair<Value, size_t>, Func&&>,
            "LruCache::compute_if_absent loader must return std::pair<Value, size_t>"
        );
        auto& shard = get_shard(key);
        std::lock_guard lock(shard.mutex);
        auto it = shard.entries.find(key);
        if (it != shard.entries.end()) {
            shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list, it->second.lru_iterator);
            m_hits.fetch_add(1, std::memory_order_relaxed);
            return it->second.value;
        }
        m_misses.fetch_add(1, std::memory_order_relaxed);

        auto [loaded, weight] = loader();

        const size_t effective_weight =
            (m_mode == CapacityMode::Count) ? size_t{1} : weight;
        if (effective_weight > shard.capacity_weight) {
            detail::log_item_too_large(effective_weight, shard.capacity_weight, "compute_if_absent");
            return loaded;
        }

        shard.evict_if_needed(effective_weight, m_evictions, m_on_evict);
        shard.lru_list.push_front(key);
        shard.entries[key] = Entry{std::move(loaded), effective_weight, shard.lru_list.begin()};
        shard.current_weight += effective_weight;

        return shard.entries[key].value;
    }

    bool contains(const Key& key) const {
        return get_shard(key).contains(key);
    }

    bool erase(const Key& key) {
        return get_shard(key).erase(key);
    }

    void clear() {
        for (auto& shard : m_shards) {
            shard->clear();
        }
        m_hits.store(0);
        m_misses.store(0);
        m_evictions.store(0);
    }

    /// Resize the cache capacity.  If the new capacity is smaller than the
    /// current weight, the least-recently-used entries are evicted (and any
    /// attached `on_evict` is fired for each) until the weight fits.  If the
    /// new capacity is zero, nothing changes.
    void resize(size_t new_capacity_weight) {
        if (new_capacity_weight == 0) return;
        const size_t shard_cap = std::max(size_t{1}, new_capacity_weight / m_shards.size());
        for (auto& shard : m_shards) {
            std::lock_guard lock(shard->mutex);
            shard->capacity_weight = shard_cap;
            shard->evict_if_needed(0, m_evictions, m_on_evict);
        }
    }

    LruCache& operator=(LruCache&& other) noexcept {
        if (this != &other) {
            m_shards = std::move(other.m_shards);
            m_hits.store(other.m_hits.load());
            m_misses.store(other.m_misses.load());
            m_evictions.store(other.m_evictions.load());
            m_mode = other.m_mode;
            m_on_evict = std::move(other.m_on_evict);
        }
        return *this;
    }

    LruCache(LruCache&& other) noexcept
        : m_mode(other.m_mode)
        , m_on_evict(std::move(other.m_on_evict))
        , m_shards(std::move(other.m_shards))
    {
        m_hits.store(other.m_hits.load());
        m_misses.store(other.m_misses.load());
        m_evictions.store(other.m_evictions.load());
    }

    Stats stats() const {
        Stats s;
        s.hits = m_hits.load(std::memory_order_relaxed);
        s.misses = m_misses.load(std::memory_order_relaxed);
        s.evictions = m_evictions.load(std::memory_order_relaxed);
        for (const auto& shard : m_shards) {
            std::lock_guard lock(shard->mutex);
            s.current_size += shard->entries.size();
            s.current_weight += shard->current_weight;
        }
        return s;
    }

private:
    struct Entry {
        Value value;
        size_t weight;
        typename std::list<Key>::iterator lru_iterator;
    };

    struct Shard {
        explicit Shard(size_t cap) : capacity_weight(cap) {}

        mutable std::mutex mutex;
        std::unordered_map<Key, Entry, Hash> entries;
        std::list<Key> lru_list;
        size_t capacity_weight;
        size_t current_weight{0};

        std::optional<Value> get(const Key& key) {
            std::lock_guard lock(mutex);
            auto it = entries.find(key);
            if (it == entries.end()) {
                return std::nullopt;
            }
            lru_list.splice(lru_list.begin(), lru_list, it->second.lru_iterator);
            return it->second.value;
        }

        void put(
            const Key& key,
            Value value,
            size_t weight,
            std::atomic<size_t>& evictions,
            const EvictCallback& on_evict)
        {
            std::lock_guard lock(mutex);
            if (weight > capacity_weight) {
                // Oversized entries are never cached. Log a warning so the caller knows
                // the item was silently dropped from the cache.
                detail::log_item_too_large(weight, capacity_weight, "put");
                return;
            }

            auto it = entries.find(key);
            if (it != entries.end()) {
                current_weight -= it->second.weight;
                lru_list.erase(it->second.lru_iterator);
                entries.erase(it);
            }

            evict_if_needed(weight, evictions, on_evict);

            lru_list.push_front(key);
            entries[key] = Entry{std::move(value), weight, lru_list.begin()};
            current_weight += weight;
        }

        bool contains(const Key& key) const {
            std::lock_guard lock(mutex);
            return entries.contains(key);
        }

        bool erase(const Key& key) {
            std::lock_guard lock(mutex);
            auto it = entries.find(key);
            if (it == entries.end()) return false;
            current_weight -= it->second.weight;
            lru_list.erase(it->second.lru_iterator);
            entries.erase(it);
            return true;
        }

        void clear() {
            std::lock_guard lock(mutex);
            entries.clear();
            lru_list.clear();
            current_weight = 0;
        }

        void evict_if_needed(
            size_t extra_weight,
            std::atomic<size_t>& evictions,
            const EvictCallback& on_evict)
        {
            while (current_weight + extra_weight > capacity_weight && !lru_list.empty()) {
                Key oldest = lru_list.back();
                auto it = entries.find(oldest);
                if (it != entries.end()) {
                    current_weight -= it->second.weight;
                    // Fire the callback BEFORE erasing so the value can be
                    // copied into the callback's captured state.
                    if (on_evict) on_evict(oldest, it->second.value);
                    entries.erase(it);
                    evictions.fetch_add(1, std::memory_order_relaxed);
                }
                lru_list.pop_back();
            }
        }
    };

    Shard& get_shard(const Key& key) {
        return *m_shards[Hash{}(key) % m_shards.size()];
    }

    const Shard& get_shard(const Key& key) const {
        return *m_shards[Hash{}(key) % m_shards.size()];
    }

    std::vector<std::unique_ptr<Shard>> m_shards;
    mutable std::atomic<size_t> m_hits{0};
    mutable std::atomic<size_t> m_misses{0};
    mutable std::atomic<size_t> m_evictions{0};
    CapacityMode m_mode{CapacityMode::Weight};
    EvictCallback m_on_evict;
};

} // namespace chronon3d::cache
