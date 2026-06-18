#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>
#include <cstring>

namespace chronon3d::video {

// ---------------------------------------------------------------------------
//  Hash
// ---------------------------------------------------------------------------

std::size_t ConvertedFrameCacheKeyHash::operator()(
    const ConvertedFrameCacheKey& k) const noexcept
{
    return static_cast<std::size_t>(
        chronon3d::core::hash::HashBuilder{}
            .add(k.framebuffer_digest)
            .add(k.width)
            .add(k.height)
            .add_enum(k.format)
            .add_enum(k.matrix)
            .add_enum(k.range)
            .add(k.apply_gamma ? 1ULL : 0ULL)
            .finish());
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

std::size_t ConvertedFrameCache::resolve_max_entries(std::size_t caller_value) {
    return resolve_cache_policy(
        cache::CacheDomain::ConvertedFrames,
        caller_value > 0 ? std::optional<std::size_t>(caller_value) : std::nullopt
    ).capacity;
}

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

ConvertedFrameCache::ConvertedFrameCache(
    std::size_t max_entries,
    std::size_t num_shards)
    : m_cache(
        [&] {
            auto cap = resolve_max_entries(max_entries);
            using namespace chronon3d::cache;
            m_diag_handle = CacheDiagnostics::instance().register_cache(
                CacheDomain::ConvertedFrames,
                [this]() -> GenericCacheStats {
                    auto s = m_cache.stats();
                    return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
                },
                [this] { m_cache.clear(); },
                [this] { return m_cache.capacity_mode(); },
                cap);
            return cap;
        }(),
        /*num_shards=*/num_shards,
        /*mode=*/cache::capacity_mode_for(cache::CacheDomain::ConvertedFrames),
        /*on_evict=*/{})
{
}

// ---------------------------------------------------------------------------
//  Lookup / insert
// ---------------------------------------------------------------------------

std::shared_ptr<const ConvertedFrameCacheEntry>
ConvertedFrameCache::lookup(const ConvertedFrameCacheKey& key) {
    // LruCache.get returns optional<shared_ptr<Entry>> (non-const).  Use the
    // explicit empty-then-static_cast dance: going via
    // optional::value_or(shared_ptr<const Entry>{}) would require constructing
    // a shared_ptr<Entry> from a shared_ptr<const Entry>, which would discard
    // the const qualifier and is rejected by shared_ptr's constructor set.
    auto opt = m_cache.get(key);
    if (!opt) return std::shared_ptr<const ConvertedFrameCacheEntry>{};
    return std::static_pointer_cast<const ConvertedFrameCacheEntry>(
        std::move(*opt));
}

void ConvertedFrameCache::insert(
    const ConvertedFrameCacheKey& key,
    const uint8_t*                data,
    std::size_t                   data_size)
{
    auto entry = std::make_shared<ConvertedFrameCacheEntry>();
    entry->key = key;
    if (data && data_size > 0) {
        entry->data.resize(data_size);
        std::memcpy(entry->data.data(), data, data_size);
        entry->data_size = data_size;
    }
    const auto weight = entry->data_size;
    m_cache.put(key, std::move(entry), weight);
}

std::shared_ptr<const ConvertedFrameCacheEntry>
ConvertedFrameCache::put_entry(
    ConvertedFrameCacheKey key,
    std::vector<uint8_t>    data)
{
    auto entry = std::make_shared<ConvertedFrameCacheEntry>();
    entry->key        = std::move(key);
    entry->data       = std::move(data);
    entry->data_size  = entry->data.size();
    const auto weight = entry->data_size;
    std::shared_ptr<const ConvertedFrameCacheEntry> shared_entry =
        std::static_pointer_cast<const ConvertedFrameCacheEntry>(entry);
    m_cache.put(entry->key, std::move(entry), weight);
    return shared_entry;
}

// ---------------------------------------------------------------------------
//  clear
// ---------------------------------------------------------------------------

void ConvertedFrameCache::clear() {
    m_cache.clear();
}

// ---------------------------------------------------------------------------
//  Diagnostics
// ---------------------------------------------------------------------------

std::size_t ConvertedFrameCache::hits() const noexcept {
    return m_cache.stats().hits;
}

std::size_t ConvertedFrameCache::misses() const noexcept {
    return m_cache.stats().misses;
}

std::size_t ConvertedFrameCache::size() const noexcept {
    return m_cache.stats().current_size;
}

auto ConvertedFrameCache::stats() const noexcept {
    return m_cache.stats();
}

} // namespace chronon3d::video
