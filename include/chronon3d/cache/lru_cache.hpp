// =============================================================================
// lru_cache.hpp — CANONICAL CACHE PRIMITIVE (thread-safe sharded LRU).
//
// Every cache instance across all three families (ContentCache, ResidencyCache,
// ProgramCache — see cache_taxonomy.hpp) MUST use this single primitive. No
// second cache engine is permitted.
// =============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chronon3d::cache::detail {
void log_item_too_large(size_t weight, size_t capacity_weight, const char* context);
} // namespace chronon3d::cache::detail

namespace chronon3d::cache {

enum class CacheRemovalReason {
    Capacity,
    Resize,
    ExplicitErase,
    Clear,
    Replace,
};

enum class CapacityMode {
    Weight,
    Count,
};

/// Canonical, thread-safe cache primitive for ContentCache, ResidencyCache and
/// ProgramCache. Detailed nanosecond timing is diagnostic-only and disabled by
/// default so Clock::now()/timing atomics never tax the normal cache hot path.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    using RemovalCallback =
        std::function<void(const Key&, const Value&, CacheRemovalReason)>;

    struct RemovedEntry {
        Key key;
        Value value;
        CacheRemovalReason reason;
    };

    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t oversized_rejections{0};
        size_t current_size{0};
        size_t current_weight{0};
        std::uint64_t hash_time_ns{0};
        std::uint64_t lock_time_ns{0};
        std::uint64_t lru_mutation_time_ns{0};
        std::uint64_t miss_loader_time_ns{0};
        std::uint64_t contention_count{0};

        [[nodiscard]] std::size_t lookups() const noexcept {
            return hits + misses;
        }
    };

    explicit LruCache(size_t capacity_weight,
                      size_t num_shards = 2,
                      CapacityMode mode = CapacityMode::Weight,
                      RemovalCallback on_remove = {},
                      bool collect_detailed_timing = false)
        : m_mode(mode),
          m_on_remove(std::move(on_remove)),
          m_collect_detailed_timing(collect_detailed_timing),
          m_shards(num_shards) {
        if (num_shards == 0) {
            throw std::invalid_argument("LruCache requires at least one shard");
        }
        size_t shard_capacity = capacity_weight / num_shards;
        if (shard_capacity == 0) shard_capacity = 1;
        for (auto& shard : m_shards) {
            shard = std::make_unique<Shard>(shard_capacity, this);
        }
    }

    [[nodiscard]] CapacityMode capacity_mode() const noexcept { return m_mode; }
    [[nodiscard]] const RemovalCallback& removal_callback() const noexcept {
        return m_on_remove;
    }
    [[nodiscard]] bool detailed_timing_enabled() const noexcept {
        return m_collect_detailed_timing;
    }

    void set_removal_callback(RemovalCallback cb) { m_on_remove = std::move(cb); }

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
        auto removed = get_shard(key).put(key, std::move(value), effective_weight);
        fire_removed(removed);
    }

    template <typename Func>
    Value compute_if_absent(const Key& key, Func&& loader) {
        static_assert(
            std::is_invocable_r_v<std::pair<Value, size_t>, Func&&>,
            "LruCache::compute_if_absent loader must return std::pair<Value, size_t>");

        auto& shard = get_shard(key);
        std::shared_future<std::pair<Value, size_t>> waiting_future;
        std::shared_ptr<std::promise<std::pair<Value, size_t>>> promise_ptr;
        std::uint64_t loader_generation = 0;

        {
            auto lock = timed_lock(shard.mutex);
            const auto lookup_start = timed_start();
            auto it = shard.entries.find(key);
            add_ns_if_enabled(m_hash_time_ns, lookup_start);
            if (it != shard.entries.end()) {
                const auto lru_start = timed_start();
                shard.lru_list.splice(shard.lru_list.begin(), shard.lru_list,
                                      it->second.lru_iterator);
                add_ns_if_enabled(m_lru_mutation_time_ns, lru_start);
                m_hits.fetch_add(1, std::memory_order_relaxed);
                return it->second.value;
            }

            loader_generation = shard.generation;
            const auto inflight_start = timed_start();
            auto inflight_it = shard.inflight.find(key);
            add_ns_if_enabled(m_hash_time_ns, inflight_start);
            if (inflight_it != shard.inflight.end() &&
                inflight_it->second.generation == loader_generation) {
                waiting_future = inflight_it->second.future;
            } else {
                m_misses.fetch_add(1, std::memory_order_relaxed);
                promise_ptr =
                    std::make_shared<std::promise<std::pair<Value, size_t>>>();
                waiting_future = promise_ptr->get_future().share();
                const auto mutate_start = timed_start();
                shard.inflight[key] = InflightLoad{loader_generation, waiting_future};
                add_ns_if_enabled(m_lru_mutation_time_ns, mutate_start);
            }
        }

        if (!promise_ptr) {
            auto result = waiting_future.get().first;
            m_hits.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        std::pair<Value, size_t> loaded_result;
        const auto loader_start = timed_start();
        try {
            loaded_result = loader();
            add_ns_if_enabled(m_miss_loader_time_ns, loader_start);
        } catch (...) {
            add_ns_if_enabled(m_miss_loader_time_ns, loader_start);
            promise_ptr->set_exception(std::current_exception());
            auto lock = timed_lock(shard.mutex);
            const auto mutate_start = timed_start();
            erase_inflight_if_generation_matches(shard, key, loader_generation);
            add_ns_if_enabled(m_lru_mutation_time_ns, mutate_start);
            throw;
        }

        const size_t effective_weight =
            (m_mode == CapacityMode::Count) ? size_t{1} : loaded_result.second;
        std::vector<RemovedEntry> evicted;
        {
            auto lock = timed_lock(shard.mutex);
            const auto mutate_start = timed_start();
            const bool generation_current = shard.generation == loader_generation;
            erase_inflight_if_generation_matches(shard, key, loader_generation);
            if (generation_current) {
                if (effective_weight > shard.capacity_weight) {
                    m_oversized_rejections.fetch_add(1, std::memory_order_relaxed);
                    detail::log_item_too_large(effective_weight, shard.capacity_weight,
                                               "compute_if_absent");
                } else {
                    evicted = shard.evict_if_needed_unlocked(
                        effective_weight, CacheRemovalReason::Capacity);
                    shard.lru_list.push_front(key);
                    shard.entries[key] = Entry{loaded_result.first, effective_weight,
                                               shard.lru_list.begin()};
                    shard.current_weight += effective_weight;
                }
            }
            add_ns_if_enabled(m_lru_mutation_time_ns, mutate_start);
        }

        // Fulfil after the insertion decision. Waiters from an old generation
        // still complete, but a clear() can never be undone by that loader.
        promise_ptr->set_value(loaded_result);
        fire_removed(evicted);
        return loaded_result.first;
    }

    bool contains(const Key& key) const { return get_shard(key).contains(key); }

    bool erase(const Key& key, bool notify = true) {
        auto removed = get_shard(key).erase(key);
        if (!removed) return false;
        if (notify && m_on_remove) {
            m_on_remove(removed->key, removed->value,
                        CacheRemovalReason::ExplicitErase);
        }
        return true;
    }

    void clear(bool notify = true) {
        std::vector<RemovedEntry> all_removed;
        for (auto& shard : m_shards) {
            auto removed = shard->clear();
            all_removed.insert(all_removed.end(),
                               std::make_move_iterator(removed.begin()),
                               std::make_move_iterator(removed.end()));
        }
        reset_counters();
        if (notify) fire_removed(all_removed);
    }

    void resize(size_t new_capacity_weight) {
        if (new_capacity_weight == 0) return;
        const size_t shard_cap =
            std::max(size_t{1}, new_capacity_weight / m_shards.size());
        for (auto& shard : m_shards) {
            std::vector<RemovedEntry> evicted;
            {
                auto lock = timed_lock(shard->mutex);
                const auto mutate_start = timed_start();
                shard->capacity_weight = shard_cap;
                evicted = shard->evict_if_needed_unlocked(
                    0, CacheRemovalReason::Resize);
                add_ns_if_enabled(m_lru_mutation_time_ns, mutate_start);
            }
            fire_removed(evicted);
        }
    }

    LruCache& operator=(LruCache&& other) noexcept {
        if (this != &other) {
            m_shards = std::move(other.m_shards);
            rebind_shards();
            copy_counters_from(other);
            m_mode = other.m_mode;
            m_on_remove = std::move(other.m_on_remove);
            m_collect_detailed_timing = other.m_collect_detailed_timing;
        }
        return *this;
    }

    LruCache(LruCache&& other) noexcept
        : m_mode(other.m_mode),
          m_on_remove(std::move(other.m_on_remove)),
          m_collect_detailed_timing(other.m_collect_detailed_timing),
          m_shards(std::move(other.m_shards)) {
        rebind_shards();
        copy_counters_from(other);
    }

    [[nodiscard]] Stats stats() const {
        Stats s;
        s.hits = m_hits.load(std::memory_order_relaxed);
        s.misses = m_misses.load(std::memory_order_relaxed);
        s.evictions = m_evictions.load(std::memory_order_relaxed);
        s.oversized_rejections =
            m_oversized_rejections.load(std::memory_order_relaxed);
        s.hash_time_ns = m_hash_time_ns.load(std::memory_order_relaxed);
        s.lock_time_ns = m_lock_time_ns.load(std::memory_order_relaxed);
        s.lru_mutation_time_ns =
            m_lru_mutation_time_ns.load(std::memory_order_relaxed);
        s.miss_loader_time_ns =
            m_miss_loader_time_ns.load(std::memory_order_relaxed);
        s.contention_count = m_contention_count.load(std::memory_order_relaxed);
        for (const auto& shard : m_shards) {
            std::lock_guard lock(shard->mutex);
            s.current_size += shard->entries.size();
            s.current_weight += shard->current_weight;
        }
        return s;
    }

    [[nodiscard]] size_t capacity() const {
        size_t total = 0;
        for (const auto& shard : m_shards) {
            std::lock_guard lock(shard->mutex);
            total += shard->capacity_weight;
        }
        return total;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        Value value;
        size_t weight;
        typename std::list<Key>::iterator lru_iterator;
    };

    struct InflightLoad {
        std::uint64_t generation{0};
        std::shared_future<std::pair<Value, size_t>> future;
    };

    struct Shard {
        Shard(size_t cap, LruCache* cache) : capacity_weight(cap), owner(cache) {}

        mutable std::mutex mutex;
        std::unordered_map<Key, Entry, Hash> entries;
        std::list<Key> lru_list;
        size_t capacity_weight;
        size_t current_weight{0};
        std::uint64_t generation{0};
        std::unordered_map<Key, InflightLoad, Hash> inflight;
        LruCache* owner{nullptr};

        std::optional<Value> get(const Key& key) {
            auto lock = owner->timed_lock(mutex);
            const auto lookup_start = owner->timed_start();
            auto it = entries.find(key);
            owner->add_ns_if_enabled(owner->m_hash_time_ns, lookup_start);
            if (it == entries.end()) return std::nullopt;
            const auto lru_start = owner->timed_start();
            lru_list.splice(lru_list.begin(), lru_list, it->second.lru_iterator);
            owner->add_ns_if_enabled(owner->m_lru_mutation_time_ns, lru_start);
            return it->second.value;
        }

        std::vector<RemovedEntry> put(const Key& key,
                                      Value value,
                                      size_t weight) {
            auto lock = owner->timed_lock(mutex);
            if (weight > capacity_weight) {
                owner->m_oversized_rejections.fetch_add(1, std::memory_order_relaxed);
                detail::log_item_too_large(weight, capacity_weight, "put");
                return {};
            }
            const auto mutate_start = owner->timed_start();
            std::vector<RemovedEntry> removed;
            auto it = entries.find(key);
            if (it != entries.end()) {
                removed.push_back(RemovedEntry{
                    key, std::move(it->second.value), CacheRemovalReason::Replace});
                current_weight -= it->second.weight;
                lru_list.erase(it->second.lru_iterator);
                entries.erase(it);
            }
            auto evicted =
                evict_if_needed_unlocked(weight, CacheRemovalReason::Capacity);
            removed.insert(removed.end(),
                           std::make_move_iterator(evicted.begin()),
                           std::make_move_iterator(evicted.end()));
            lru_list.push_front(key);
            entries[key] = Entry{std::move(value), weight, lru_list.begin()};
            current_weight += weight;
            owner->add_ns_if_enabled(owner->m_lru_mutation_time_ns, mutate_start);
            return removed;
        }

        bool contains(const Key& key) const {
            auto lock = owner->timed_lock(mutex);
            const auto lookup_start = owner->timed_start();
            const bool found = entries.contains(key);
            owner->add_ns_if_enabled(owner->m_hash_time_ns, lookup_start);
            return found;
        }

        std::optional<RemovedEntry> erase(const Key& key) {
            auto lock = owner->timed_lock(mutex);
            const auto lookup_start = owner->timed_start();
            auto it = entries.find(key);
            owner->add_ns_if_enabled(owner->m_hash_time_ns, lookup_start);
            if (it == entries.end()) return std::nullopt;
            const auto mutate_start = owner->timed_start();
            current_weight -= it->second.weight;
            lru_list.erase(it->second.lru_iterator);
            RemovedEntry entry{key, std::move(it->second.value),
                               CacheRemovalReason::ExplicitErase};
            entries.erase(it);
            owner->add_ns_if_enabled(owner->m_lru_mutation_time_ns, mutate_start);
            return entry;
        }

        std::vector<RemovedEntry> clear() {
            auto lock = owner->timed_lock(mutex);
            const auto mutate_start = owner->timed_start();
            std::vector<RemovedEntry> removed;
            removed.reserve(entries.size());
            for (auto& [k, entry] : entries) {
                removed.push_back(RemovedEntry{
                    k, std::move(entry.value), CacheRemovalReason::Clear});
            }
            ++generation;
            entries.clear();
            lru_list.clear();
            inflight.clear();
            current_weight = 0;
            owner->add_ns_if_enabled(owner->m_lru_mutation_time_ns, mutate_start);
            return removed;
        }

        std::vector<RemovedEntry> evict_if_needed_unlocked(
            size_t extra_weight,
            CacheRemovalReason reason) {
            std::vector<RemovedEntry> removed;
            while (current_weight + extra_weight > capacity_weight &&
                   !lru_list.empty()) {
                Key oldest = lru_list.back();
                auto it = entries.find(oldest);
                if (it != entries.end()) {
                    current_weight -= it->second.weight;
                    removed.push_back(RemovedEntry{
                        oldest, std::move(it->second.value), reason});
                    entries.erase(it);
                }
                lru_list.pop_back();
            }
            return removed;
        }
    };

    [[nodiscard]] Clock::time_point timed_start() const noexcept {
        return m_collect_detailed_timing ? Clock::now() : Clock::time_point{};
    }

    [[nodiscard]] std::unique_lock<std::mutex> timed_lock(std::mutex& mutex) const {
        if (!m_collect_detailed_timing) {
            return std::unique_lock<std::mutex>(mutex);
        }
        const auto start = Clock::now();
        if (mutex.try_lock()) {
            add_ns_if_enabled(m_lock_time_ns, start);
            return std::unique_lock<std::mutex>(mutex, std::adopt_lock);
        }
        m_contention_count.fetch_add(1, std::memory_order_relaxed);
        mutex.lock();
        add_ns_if_enabled(m_lock_time_ns, start);
        return std::unique_lock<std::mutex>(mutex, std::adopt_lock);
    }

    void add_ns_if_enabled(std::atomic<std::uint64_t>& counter,
                           Clock::time_point start) const noexcept {
        if (!m_collect_detailed_timing) return;
        const auto elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
                .count());
        counter.fetch_add(elapsed, std::memory_order_relaxed);
    }

    void erase_inflight_if_generation_matches(Shard& shard,
                                               const Key& key,
                                               std::uint64_t generation) {
        auto it = shard.inflight.find(key);
        if (it != shard.inflight.end() && it->second.generation == generation) {
            shard.inflight.erase(it);
        }
    }

    void fire_removed(const std::vector<RemovedEntry>& removed) {
        for (const auto& entry : removed) {
            if (entry.reason == CacheRemovalReason::Capacity ||
                entry.reason == CacheRemovalReason::Resize) {
                m_evictions.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_on_remove) m_on_remove(entry.key, entry.value, entry.reason);
        }
    }

    Shard& get_shard(const Key& key) {
        const auto start = timed_start();
        const auto hash = Hash{}(key);
        add_ns_if_enabled(m_hash_time_ns, start);
        return *m_shards[hash % m_shards.size()];
    }

    const Shard& get_shard(const Key& key) const {
        const auto start = timed_start();
        const auto hash = Hash{}(key);
        add_ns_if_enabled(m_hash_time_ns, start);
        return *m_shards[hash % m_shards.size()];
    }

    void reset_counters() noexcept {
        m_hits.store(0, std::memory_order_relaxed);
        m_misses.store(0, std::memory_order_relaxed);
        m_evictions.store(0, std::memory_order_relaxed);
        m_oversized_rejections.store(0, std::memory_order_relaxed);
        m_hash_time_ns.store(0, std::memory_order_relaxed);
        m_lock_time_ns.store(0, std::memory_order_relaxed);
        m_lru_mutation_time_ns.store(0, std::memory_order_relaxed);
        m_miss_loader_time_ns.store(0, std::memory_order_relaxed);
        m_contention_count.store(0, std::memory_order_relaxed);
    }

    void copy_counters_from(const LruCache& other) noexcept {
        m_hits.store(other.m_hits.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_misses.store(other.m_misses.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_evictions.store(other.m_evictions.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_oversized_rejections.store(
            other.m_oversized_rejections.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        m_hash_time_ns.store(other.m_hash_time_ns.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_lock_time_ns.store(other.m_lock_time_ns.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_lru_mutation_time_ns.store(
            other.m_lru_mutation_time_ns.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_miss_loader_time_ns.store(
            other.m_miss_loader_time_ns.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_contention_count.store(
            other.m_contention_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void rebind_shards() noexcept {
        for (auto& shard : m_shards) {
            if (shard) shard->owner = this;
        }
    }

    CapacityMode m_mode{CapacityMode::Weight};
    RemovalCallback m_on_remove;
    bool m_collect_detailed_timing{false};
    std::vector<std::unique_ptr<Shard>> m_shards;
    mutable std::atomic<size_t> m_hits{0};
    mutable std::atomic<size_t> m_misses{0};
    mutable std::atomic<size_t> m_evictions{0};
    mutable std::atomic<size_t> m_oversized_rejections{0};
    mutable std::atomic<std::uint64_t> m_hash_time_ns{0};
    mutable std::atomic<std::uint64_t> m_lock_time_ns{0};
    mutable std::atomic<std::uint64_t> m_lru_mutation_time_ns{0};
    mutable std::atomic<std::uint64_t> m_miss_loader_time_ns{0};
    mutable std::atomic<std::uint64_t> m_contention_count{0};
};

} // namespace chronon3d::cache
