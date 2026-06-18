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
 * @brief Reason an entry was removed from the cache.
 *
 * Attached to every RemovalCallback invocation so downstream code
 * can distinguish capacity-driven evictions from explicit erases,
 * replaces, clears, and resize-driven evictions.
 */
enum class CacheRemovalReason {
    /// Capacity-driven LRU eviction (put / compute_if_absent overflow).
    Capacity,
    /// Resize-driven LRU eviction (shrink capacity).
    Resize,
    /// Explicit erase() call.
    ExplicitErase,
    /// Clear of the whole cache.
    Clear,
    /// An existing entry was replaced by a new put() for the same key.
    Replace,
};

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
    /// Callback invoked OUTSIDE the shard mutex for every entry removal
    /// (eviction, resize, erase, clear, replace).  Receives the key,
    /// a const ref to the value being removed, and the reason.
    ///
    /// Because the callback fires after the shard lock is released, it is
    /// safe to interact with other caches (including other LruCache
    /// instances) without deadlock risk.
    ///
    /// Must still NOT re-enter THIS same cache (put/get/erase/clear/resize)
    /// from inside the callback — that would re-acquire a shard mutex and
    /// may deadlock if the re-entered path tries the same shard.
    using RemovalCallback = std::function<void(const Key&, const Value&, CacheRemovalReason)>;

    /// One removed entry, used internally to defer callbacks outside the shard lock.
    struct RemovedEntry {
        Key                   key;
        Value                 value;
        CacheRemovalReason    reason;
    };

    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t current_size{0};
        size_t current_weight{0};
    };

    /// Construct a cache with `capacity_weight` units per shard split across
    /// `num_shards` shards.  In Count mode every entry contributes weight 1
    /// regardless of the per-call weight.  `on_remove` (optional) fires for
    /// every entry removal OUTSIDE the shard mutex.
    explicit LruCache(
        size_t capacity_weight,
        size_t num_shards = 2,
        CapacityMode mode = CapacityMode::Weight,
        RemovalCallback on_remove = {})
        : m_mode(mode)
        , m_on_remove(std::move(on_remove))
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

    /// Read-only accessor for the attached removal callback.
    [[nodiscard]] const RemovalCallback& removal_callback() const noexcept { return m_on_remove; }

    /// Replace the removal callback (caution: non-atomic across threads).
    void set_removal_callback(RemovalCallback cb) {
        m_on_remove = std::move(cb);
    }

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

    /// Insert or replace an entry.  If the key already exists the old value
    /// is removed with reason `Replace`.  If the insertion triggers LRU
    /// evictions, those fire with reason `Capacity`.
    ///
    /// All callbacks fire OUTSIDE the shard mutex (deferred).
    void put(const Key& key, Value value, size_t weight = 1) {
        const size_t effective_weight =
            (m_mode == CapacityMode::Count) ? size_t{1} : weight;
        auto removed = get_shard(key).put(key, std::move(value), effective_weight);
        // Fire callbacks after lock is released.
        fire_removed(removed);
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
    ///   - Eviction callbacks fire OUTSIDE the shard lock (deferred).
    template <typename Func>
    Value compute_if_absent(const Key& key, Func&& loader) {
        static_assert(
            std::is_invocable_r_v<std::pair<Value, size_t>, Func&&>,
            "LruCache::compute_if_absent loader must return std::pair<Value, size_t>"
        );
        auto& shard = get_shard(key);
        std::vector<RemovedEntry> evicted;
        Value result;
        {
            std::lock_guard lock(shard.mutex);
            auto it = shard.entries.find(key);
            if (it != shard.entries.end()) {
                shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                                      it->second.lru_iterator);
                m_hits.fetch_add(1, std::memory_order_relaxed);
                return it->second.value;
            }
            m_misses.fetch_add(1, std::memory_order_relaxed);

            auto [loaded, weight] = loader();

            const size_t effective_weight =
                (m_mode == CapacityMode::Count) ? size_t{1} : weight;
            if (effective_weight > shard.capacity_weight) {
                detail::log_item_too_large(effective_weight, shard.capacity_weight,
                                           "compute_if_absent");
                return loaded;
            }

            evicted = shard.evict_if_needed(effective_weight, CacheRemovalReason::Capacity);
            shard.lru_list.push_front(key);
            shard.entries[key] = Entry{std::move(loaded), effective_weight,
                                       shard.lru_list.begin()};
            shard.current_weight += effective_weight;
            result = shard.entries[key].value;
        }
        // Fire callbacks after lock is released.
        fire_removed(evicted);
        return result;
    }

    bool contains(const Key& key) const {
        return get_shard(key).contains(key);
    }

    /// Erase a specific entry.  When `notify` is true (default), the removal
    /// callback fires with reason `ExplicitErase` OUTSIDE the shard mutex.
    /// Returns whether the entry existed.
    bool erase(const Key& key, bool notify = true) {
        auto removed = get_shard(key).erase(key);
        if (removed) {
            if (notify && m_on_remove) {
                m_on_remove(removed->key, removed->value,
                            CacheRemovalReason::ExplicitErase);
            }
            return true;
        }
        return false;
    }

    /// Clear all entries.  When `notify` is true (default), the removal
    /// callback fires for every entry with reason `Clear` OUTSIDE the shard
    /// mutexes.  Resets hit/miss/eviction counters.
    void clear(bool notify = true) {
        std::vector<RemovedEntry> all_removed;
        for (auto& shard : m_shards) {
            auto removed = shard->clear();
            all_removed.insert(all_removed.end(),
                               std::make_move_iterator(removed.begin()),
                               std::make_move_iterator(removed.end()));
        }
        m_hits.store(0);
        m_misses.store(0);
        m_evictions.store(0);

        if (notify) {
            fire_removed(all_removed);
        }
    }

    /// Resize the cache capacity.  If the new capacity is smaller than the
    /// current weight, the least-recently-used entries are evicted with
    /// reason `Resize` (callbacks fire OUTSIDE the shard mutexes).
    /// If the new capacity is zero, nothing changes.
    void resize(size_t new_capacity_weight) {
        if (new_capacity_weight == 0) return;
        const size_t shard_cap =
            std::max(size_t{1}, new_capacity_weight / m_shards.size());
        for (auto& shard : m_shards) {
            std::vector<RemovedEntry> evicted;
            {
                std::lock_guard lock(shard->mutex);
                shard->capacity_weight = shard_cap;
                evicted = shard->evict_if_needed(0, CacheRemovalReason::Resize);
            }
            fire_removed(evicted);
        }
    }

    LruCache& operator=(LruCache&& other) noexcept {
        if (this != &other) {
            m_shards = std::move(other.m_shards);
            m_hits.store(other.m_hits.load());
            m_misses.store(other.m_misses.load());
            m_evictions.store(other.m_evictions.load());
            m_mode = other.m_mode;
            m_on_remove = std::move(other.m_on_remove);
        }
        return *this;
    }

    LruCache(LruCache&& other) noexcept
        : m_mode(other.m_mode)
        , m_on_remove(std::move(other.m_on_remove))
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

        /// Insert or replace an entry.  Returns a vector of removed entries
        /// (replace + capacity evictions) for deferred callback notification.
        /// The caller MUST fire callbacks outside the mutex.
        /// Returns empty vector if the item is oversized (not stored).
        std::vector<RemovedEntry> put(
            const Key& key,
            Value value,
            size_t weight)
        {
            std::lock_guard lock(mutex);
            if (weight > capacity_weight) {
                detail::log_item_too_large(weight, capacity_weight, "put");
                return {};
            }

            std::vector<RemovedEntry> removed;

            auto it = entries.find(key);
            if (it != entries.end()) {
                // Replacing an existing entry — collect the old value.
                removed.push_back(RemovedEntry{
                    key,
                    std::move(it->second.value),
                    CacheRemovalReason::Replace});
                current_weight -= it->second.weight;
                lru_list.erase(it->second.lru_iterator);
                entries.erase(it);
            }

            auto evicted = evict_if_needed(weight, CacheRemovalReason::Capacity);
            removed.insert(removed.end(),
                           std::make_move_iterator(evicted.begin()),
                           std::make_move_iterator(evicted.end()));

            lru_list.push_front(key);
            entries[key] = Entry{std::move(value), weight, lru_list.begin()};
            current_weight += weight;

            return removed;
        }

        bool contains(const Key& key) const {
            std::lock_guard lock(mutex);
            return entries.contains(key);
        }

        /// Erase a specific entry.  Returns the removed entry for deferred
        /// callback notification, or nullopt if not found.
        std::optional<RemovedEntry> erase(const Key& key) {
            std::lock_guard lock(mutex);
            auto it = entries.find(key);
            if (it == entries.end()) return std::nullopt;

            current_weight -= it->second.weight;
            lru_list.erase(it->second.lru_iterator);

            RemovedEntry entry{key, std::move(it->second.value),
                               CacheRemovalReason::ExplicitErase};
            entries.erase(it);
            return entry;
        }

        /// Clear all entries.  Returns all removed entries for deferred
        /// callback notification.
        std::vector<RemovedEntry> clear() {
            std::lock_guard lock(mutex);
            std::vector<RemovedEntry> removed;
            removed.reserve(entries.size());
            for (auto& [k, entry] : entries) {
                removed.push_back(RemovedEntry{k, std::move(entry.value),
                                                CacheRemovalReason::Clear});
            }
            entries.clear();
            lru_list.clear();
            current_weight = 0;
            return removed;
        }

        /// Evict LRU-tail entries until the shard weight fits.
        /// @param extra_weight  Additional weight the caller is about to add.
        /// @param reason        Reason for the eviction (Capacity or Resize).
        /// @return              Vector of removed entries for deferred callback
        ///                      notification.
        std::vector<RemovedEntry> evict_if_needed(
            size_t extra_weight,
            CacheRemovalReason reason)
        {
            std::vector<RemovedEntry> removed;
            while (current_weight + extra_weight > capacity_weight && !lru_list.empty()) {
                Key oldest = lru_list.back();
                auto it = entries.find(oldest);
                if (it != entries.end()) {
                    current_weight -= it->second.weight;
                    removed.push_back(RemovedEntry{oldest, std::move(it->second.value),
                                                    reason});
                    entries.erase(it);
                }
                lru_list.pop_back();
            }
            return removed;
        }
    };

    /// Fire the removal callback (if set) for every entry in `removed`,
    /// and bump the eviction counter for Capacity/Resize reasons.
    void fire_removed(const std::vector<RemovedEntry>& removed) {
        for (const auto& entry : removed) {
            if (entry.reason == CacheRemovalReason::Capacity ||
                entry.reason == CacheRemovalReason::Resize) {
                m_evictions.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_on_remove) {
                m_on_remove(entry.key, entry.value, entry.reason);
            }
        }
    }

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
    RemovalCallback m_on_remove;
};

} // namespace chronon3d::cache
