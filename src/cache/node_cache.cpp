#include <chronon3d/cache/node_cache.hpp>

#include <string_view>
#include <utility>

namespace chronon3d::cache {

u64 NodeCacheKey::digest() const {
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    XXH64_update(state, scope.data(), scope.size());
    XXH64_update(state, &frame, sizeof(frame));
    XXH64_update(state, &width, sizeof(width));
    XXH64_update(state, &height, sizeof(height));
    XXH64_update(state, &params_hash, sizeof(params_hash));
    XXH64_update(state, &source_hash, sizeof(source_hash));
    XXH64_update(state, &input_hash, sizeof(input_hash));
    u64 hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

NodeCache::NodeCache(usize capacity_bytes)
    : m_mutex(std::make_unique<std::mutex>()), m_capacity_bytes(capacity_bytes)
{
}

NodeCache::Value NodeCache::get(u64 key)
{
    std::lock_guard lock(*m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        m_stats.misses++;
        return nullptr;
    }

    touch(key);
    m_stats.hits++;
    return it->second.value;
}

void NodeCache::put(u64 key, Value value, usize size_bytes)
{
    std::lock_guard lock(*m_mutex);
    
    if (auto it = m_entries.find(key); it != m_entries.end()) {
        m_stats.current_usage_bytes -= it->second.size_bytes;
        m_entries.erase(it);
        // lru handled by touch in the next step
    }

    evict_if_needed(size_bytes);

    m_lru_list.push_front(key);
    m_entries[key] = Entry{
        .value = std::move(value),
        .size_bytes = size_bytes,
        .lru_iterator = m_lru_list.begin()
    };
    
    m_stats.current_usage_bytes += size_bytes;
}

bool NodeCache::erase(u64 key)
{
    std::lock_guard lock(*m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) return false;
    m_stats.current_usage_bytes -= it->second.size_bytes;
    m_lru_list.erase(it->second.lru_iterator);
    m_entries.erase(it);
    return true;
}

bool NodeCache::contains(u64 key) const
{
    std::lock_guard lock(*m_mutex);
    return m_entries.contains(key);
}

void NodeCache::clear()
{
    std::lock_guard lock(*m_mutex);
    m_entries.clear();
    m_lru_list.clear();
    m_stats.current_usage_bytes = 0;
}

void NodeCache::set_capacity(usize capacity_bytes)
{
    std::lock_guard lock(*m_mutex);
    m_capacity_bytes = capacity_bytes;
    evict_if_needed(0);
}

void NodeCache::evict_if_needed(usize required_bytes)
{
    while (!m_lru_list.empty() && m_stats.current_usage_bytes + required_bytes > m_capacity_bytes) {
        u64 key_to_evict = m_lru_list.back();
        auto it = m_entries.find(key_to_evict);
        if (it != m_entries.end()) {
            m_stats.current_usage_bytes -= it->second.size_bytes;
            m_entries.erase(it);
        }
        m_lru_list.pop_back();
        m_stats.evictions++;
    }
}

void NodeCache::touch(u64 key)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        m_lru_list.erase(it->second.lru_iterator);
        m_lru_list.push_front(key);
        it->second.lru_iterator = m_lru_list.begin();
    }
}

} // namespace chronon3d::cache
