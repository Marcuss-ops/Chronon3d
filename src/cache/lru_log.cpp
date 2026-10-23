// ============================================================================
// lru_log.cpp — Non-template logging helpers for LruCache.
//
// Out-of-line logging keeps spdlog out of the LruCache template header.
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
