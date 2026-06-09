#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <algorithm>
#include <cstring>
#include <limits>

namespace chronon3d::video {

ConvertedFrameCache::ConvertedFrameCache(size_t capacity)
    : capacity_(capacity)
{
    entries_.reserve(capacity);
}

const ConvertedFrameCacheEntry* ConvertedFrameCache::lookup(
    const ConvertedFrameCacheKey& key)
{
    for (auto& entry : entries_) {
        if (entry.key == key) {
            entry.last_used = ++access_clock_;
            ++hits_;
            return &entry;
        }
    }
    ++misses_;
    return nullptr;
}

void ConvertedFrameCache::insert(
    const ConvertedFrameCacheKey& key,
    const uint8_t*                data,
    size_t                        data_size)
{
    // Update existing entry if key already present (digest changed → false hit
    // is impossible; same key means same output).
    for (auto& entry : entries_) {
        if (entry.key == key) {
            if (entry.data.size() < data_size) entry.data.resize(data_size);
            std::memcpy(entry.data.data(), data, data_size);
            entry.data_size = data_size;
            entry.last_used = ++access_clock_;
            return;
        }
    }

    // Find a free slot or evict LRU.
    if (entries_.size() < capacity_) {
        auto& e = entries_.emplace_back();
        e.key  = key;
        e.data.resize(data_size);
        std::memcpy(e.data.data(), data, data_size);
        e.data_size = data_size;
        e.last_used = ++access_clock_;
    } else {
        // Evict least-recently-used slot.
        const size_t lru = find_lru_slot();
        auto& e   = entries_[lru];
        e.key     = key;
        if (e.data.size() < data_size) e.data.resize(data_size);
        std::memcpy(e.data.data(), data, data_size);
        e.data_size = data_size;
        e.last_used = ++access_clock_;
    }
}

void ConvertedFrameCache::clear() {
    entries_.clear();
    access_clock_ = 0;
    hits_         = 0;
    misses_       = 0;
}

size_t ConvertedFrameCache::find_lru_slot() const {
    size_t   lru_idx  = 0;
    uint64_t lru_time = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].last_used < lru_time) {
            lru_time = entries_[i].last_used;
            lru_idx  = i;
        }
    }
    return lru_idx;
}

} // namespace chronon3d::video
