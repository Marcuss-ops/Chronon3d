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

std::size_t ConvertedFrameCache::resolve_capacity_bytes(std::size_t caller_value) {
    return resolve_cache_policy(
        cache::CacheDomain::ConvertedFrames,
        caller_value > 0 ? std::optional<std::size_t>(caller_value) : std::nullopt
    ).capacity;
}

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

ConvertedFrameCache::ConvertedFrameCache(
    std::size_t capacity_bytes,
    std::size_t num_shards,
    chronon3d::cache::CacheDiagnostics* diag)
    : m_cache(
        [&] {
            auto cap = resolve_capacity_bytes(capacity_bytes);
            using namespace chronon3d::cache;
            if (diag) {
                m_diag_handle = diag->register_cache(
                    CacheDomain::ConvertedFrames,
                    [this]() -> GenericCacheStats {
                        auto s = m_cache.stats();
                        return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
                    },
                    [this] { m_cache.clear(); },
                    [this] { return m_cache.capacity_mode(); },
                    cap);
            }
            return cap;
        }(),
        /*num_shards=*/num_shards,
        /*mode=*/cache::capacity_mode_for(cache::CacheDomain::ConvertedFrames),
        /*on_evict=*/{})
{
}

void ConvertedFrameCache::set_diagnostics(chronon3d::cache::CacheDiagnostics& diag) {
    m_diag_alive.store(false, std::memory_order_release);
    m_diag_handle = {};
    m_diag_alive.store(true, std::memory_order_release);
    using namespace chronon3d::cache;
    m_diag_handle = diag.register_cache(
        CacheDomain::ConvertedFrames,
        [this]() -> GenericCacheStats {
            if (!m_diag_alive.load(std::memory_order_acquire)) return {};
            auto s = m_cache.stats();
            return {s.hits, s.misses, s.evictions, s.current_size, s.current_weight};
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return;
            m_cache.clear();
        },
        [this] {
            if (!m_diag_alive.load(std::memory_order_acquire)) return CapacityMode::Weight;
            return m_cache.capacity_mode();
        },
        resolve_capacity_bytes(0));
}

// ---------------------------------------------------------------------------
//  Lookup / insert
// ---------------------------------------------------------------------------

ConvertedFrame ConvertedFrameCache::lookup(const ConvertedFrameCacheKey& key) {
    auto opt = m_cache.get(key);
    if (!opt) return ConvertedFrame{};

    const auto& entry = *opt;
    return ConvertedFrame{
        .cache_entry = entry,
        .data        = std::span<const uint8_t>(entry->data.data(), entry->data.size()),
        .from_cache  = true,
    };
}

ConvertedFrame ConvertedFrameCache::insert(
    ConvertedFrameCacheKey key,
    std::vector<uint8_t>   data)
{
    auto entry = std::make_shared<ConvertedFrameCacheEntry>();
    entry->key = std::move(key);
    entry->data = std::move(data);

    const auto weight = entry->data.size();

    m_cache.put(entry->key, entry, weight);

    return ConvertedFrame{
        .cache_entry = entry,
        .data        = std::span<const uint8_t>(entry->data.data(), entry->data.size()),
        .from_cache  = true,
    };
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

auto ConvertedFrameCache::stats() const noexcept -> decltype(m_cache.stats()) {
    return m_cache.stats();
}

} // namespace chronon3d::video
