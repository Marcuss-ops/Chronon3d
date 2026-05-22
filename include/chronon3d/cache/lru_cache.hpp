#pragma once

#include <atomic>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace chronon3d::cache {

/**
 * @brief A generic, thread-safe LRU cache with sharding to reduce mutex contention.
 */
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LruCache {
public:
    struct Stats {
        size_t hits{0};
        size_t misses{0};
        size_t evictions{0};
        size_t current_size{0};
        size_t current_weight{0};
    };

    explicit LruCache(size_t capacity_weight, size_t num_shards = 16)
        : m_shards(num_shards) {
        size_t shard_capacity = capacity_weight / num_shards;
        if (shard_capacity == 0) shard_capacity = 1;
        for (auto& shard : m_shards) {
            shard = std::make_unique<Shard>(shard_capacity);
        }
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

    void put(const Key& key, Value value, size_t weight = 1) {
        get_shard(key).put(key, std::move(value), weight, m_evictions);
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

    LruCache& operator=(LruCache&& other) noexcept {
        if (this != &other) {
            m_shards = std::move(other.m_shards);
            m_hits.store(other.m_hits.load());
            m_misses.store(other.m_misses.load());
            m_evictions.store(other.m_evictions.load());
        }
        return *this;
    }

    LruCache(LruCache&& other) noexcept
        : m_shards(std::move(other.m_shards)) {
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

        void put(const Key& key, Value value, size_t weight, std::atomic<size_t>& evictions) {
            std::lock_guard lock(mutex);
            if (weight > capacity_weight) {
                // Oversized entries are never cached. This keeps a single large framebuffer
                // from consuming an entire shard and bypassing the intended capacity bound.
                return;
            }

            auto it = entries.find(key);
            if (it != entries.end()) {
                current_weight -= it->second.weight;
                lru_list.erase(it->second.lru_iterator);
                entries.erase(it);
            }

            evict_if_needed(weight, evictions);

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

        void evict_if_needed(size_t extra_weight, std::atomic<size_t>& evictions) {
            while (current_weight + extra_weight > capacity_weight && !lru_list.empty()) {
                Key oldest = lru_list.back();
                auto it = entries.find(oldest);
                if (it != entries.end()) {
                    current_weight -= it->second.weight;
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
};

} // namespace chronon3d::cache
