// ============================================================================
// lru_log.cpp — Non-template logging helpers for LruCache.
//
// Extracted from lru_cache.hpp so the template header doesn't need to
// #include <spdlog/spdlog.h>.  Template methods call these helpers when
// they need to log warnings about oversized items.
// ============================================================================

#include <chronon3d/cache/lru_cache.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cache::detail {

void log_item_too_large(size_t weight, size_t capacity_weight, const char* context) {
    spdlog::warn(
        "[LRU] {}: item weight {} exceeds shard capacity {} - not cached",
        context,
        weight,
        capacity_weight
    );
}

} // namespace chronon3d::cache::detail
