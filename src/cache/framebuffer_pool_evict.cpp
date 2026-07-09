// ============================================================================
// framebuffer_pool_evict.cpp — Eviction + preallocation + warm-up for FramebufferPool
//
// Moved-out contents (FASE 18):
//   - evict_one_from_bucket(): LRU eviction within a size class
//   - evict_global_lru(): cross-bucket LRU eviction
//   - evict_lru_for(): eviction driver (loop until room available)
//   - round_to_bucket(): width/height bucket rounding (static public)
//   - do_preallocate_into_bucket(): anonymous helper, fills a bucket with FBs
//   - preallocate(): public entry point (mutex-locking wrapper)
//   - ensure_preallocated(): minimum-count ensure (mutex-locking wrapper)
//   - warm_up(): bulk preallocation of predictions (public entry point)
//   - the anonymous `round_up_bucket()` helper (internal rounding)
//
// The header (framebuffer_pool.hpp) is included unchanged, so all private
// state access (m_free, m_mutex, m_config, m_evicted_*, etc.) works as-is.
// Logic is preserved verbatim from the original — only relocated.
// ============================================================================

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace chronon3d::cache {

namespace {

// round_up_bucket — width/height rounding into pool size classes.
// Differs step size for small/medium/large to keep size classes bounded.
int round_up_bucket(int val) {
    if (val <= 0) return 0;
    if (val <= 64) {
        return ((val + 7) / 8) * 8;
    } else if (val <= 256) {
        return ((val + 15) / 16) * 16;
    } else if (val <= 1024) {
        return ((val + 127) / 128) * 128;
    } else {
        return ((val + 127) / 128) * 128;
    }
}

} // namespace (round_up_bucket helper)

// ---------------------------------------------------------------------------
// evict_one_from_bucket — LRU eviction within a single size class.
//
// Searches the bucket for the entry with the smallest last_used_tick and
// removes it (swap-with-back + pop_back idiom).  Returns false when the
// bucket is empty.  Updates lifetime counters and frees the bucket key
// when it becomes empty (avoid unbounded growth in m_free).
// ---------------------------------------------------------------------------
bool FramebufferPool::evict_one_from_bucket(FramebufferPoolKey key) {
    auto it = m_free.find(key);
    if (it == m_free.end() || it->second.empty()) return false;

    auto& bucket = it->second;
    // TICKET-O(n)-audit — std::min_element replaces the hand-rolled
    // linear min-tick scan. Bit-identical: the same LRU entry is
    // returned. Per-bucket size is bounded by max_buffers_per_size_class
    // (typically 8-16), so the O(n) cost is already small; the win is
    // readability + branch-friendly codegen.
    auto lru_it = std::min_element(
        bucket.begin(), bucket.end(),
        [](const PoolEntry& a, const PoolEntry& b) noexcept {
            return a.last_used_tick < b.last_used_tick;
        });
    if (lru_it == bucket.end()) return false;
    const size_t lru_idx = static_cast<size_t>(
        std::distance(bucket.begin(), lru_it));

    size_t evicted_bytes = bucket[lru_idx].fb->size_bytes();
    bucket[lru_idx] = std::move(bucket.back());
    bucket.pop_back();
    m_current_bytes -= evicted_bytes;
    m_evicted_count.fetch_add(1, std::memory_order_relaxed);
    m_evicted_bytes.fetch_add(evicted_bytes, std::memory_order_relaxed);
    m_pressure_count.fetch_add(1, std::memory_order_relaxed);

    if (bucket.empty()) {
        m_free.erase(it);
    }
    return true;
}

// ---------------------------------------------------------------------------
// evict_global_lru — cross-bucket LRU eviction.
//
// Walks every bucket to find the globally oldest entry, evicts it.
// Returns false when all buckets are empty.
// ---------------------------------------------------------------------------
bool FramebufferPool::evict_global_lru() {
    std::optional<FramebufferPoolKey> lru_key;
    size_t                              lru_idx  = 0;
    uint64_t                            min_tick = std::numeric_limits<uint64_t>::max();

    for (auto& [key, bucket] : m_free) {
        if (bucket.empty()) continue;
        // TICKET-O(n)-audit — std::min_element replaces the hand-rolled
        // inner linear scan; bit-identical globally-LRU entry is still
        // picked. Readability + branch-friendly codegen is the win.
        auto local_lru = std::min_element(
            bucket.begin(), bucket.end(),
            [](const PoolEntry& a, const PoolEntry& b) noexcept {
                return a.last_used_tick < b.last_used_tick;
            });
        if (local_lru == bucket.end()) continue;
        if (local_lru->last_used_tick < min_tick) {
            min_tick = local_lru->last_used_tick;
            lru_key  = key;
            lru_idx  = static_cast<size_t>(
                std::distance(bucket.begin(), local_lru));
        }
    }

    if (!lru_key.has_value()) return false;

    auto it = m_free.find(*lru_key);
    if (it == m_free.end()) return false;

    auto& bucket = it->second;
    if (lru_idx >= bucket.size()) return false;

    size_t evicted_bytes = bucket[lru_idx].fb->size_bytes();
    bucket[lru_idx] = std::move(bucket.back());
    bucket.pop_back();
    m_current_bytes -= evicted_bytes;
    m_evicted_count.fetch_add(1, std::memory_order_relaxed);
    m_evicted_bytes.fetch_add(evicted_bytes, std::memory_order_relaxed);
    m_pressure_count.fetch_add(1, std::memory_order_relaxed);

    if (bucket.empty()) {
        m_free.erase(it);
    }
    return true;
}

// ---------------------------------------------------------------------------
// evict_lru_for — eviction driver: make room for `incoming_bytes`.
// ---------------------------------------------------------------------------
bool FramebufferPool::evict_lru_for(size_t incoming_bytes) {
    if (m_config.max_retained_bytes == 0) return true; // unlimited

    bool evicted_any = false;
    while (m_current_bytes + incoming_bytes > m_config.max_retained_bytes) {
        if (!evict_global_lru()) break;
        evicted_any = true;
    }
    return evicted_any;
}

// ---------------------------------------------------------------------------
// round_to_bucket — width/height → bucket key (static, public).
// ---------------------------------------------------------------------------
std::pair<int, int> FramebufferPool::round_to_bucket(int width, int height) {
    return {round_up_bucket(width), round_up_bucket(height)};
}

namespace {

// do_preallocate_into_bucket — fills a bucket with FBs up to the per-bucket
// and total budget caps.  Honors `clear` and `touch_memory` options.
size_t do_preallocate_into_bucket(
    std::unordered_map<
        FramebufferPoolKey,
        std::vector<PoolEntry>,
        FramebufferPoolKeyHash
    >& free_map,
    size_t& current_bytes,
    size_t max_bytes,
    const FramebufferPoolPreallocOptions& options,
    int rounded_w,
    int rounded_h,
    size_t count)
{
    FramebufferPoolKey key{rounded_w, rounded_h};
    size_t created = 0;
    for (size_t i = 0; i < count; ++i) {
        auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h);

        if (options.clear) {
            fb->clear(Color::transparent());
        } else if (options.touch_memory) {
            Color* ptr = fb->data();
            for (i32 y = 0; y < rounded_h; ++y) {
                Color* row = ptr + static_cast<size_t>(y) * fb->stride();
                for (i32 x = 0; x < rounded_w; ++x) {
                    Color c = row[x];
                    row[x] = c;
                }
            }
            fb->set_content_cleared(true);
        }

        const size_t weight = fb->size_bytes();
        if (max_bytes > 0 && current_bytes + weight > max_bytes) {
            break;
        }

        current_bytes += weight;
        free_map[key].push_back(PoolEntry{std::move(fb), 0});
        ++created;
    }
    return created;
}

} // namespace (do_preallocate_into_bucket helper)

size_t FramebufferPool::preallocate(const FramebufferPoolPreallocOptions& options) {
    if (options.width <= 0 || options.height <= 0 || options.count == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const int rounded_w = round_up_bucket(options.width);
    const int rounded_h = round_up_bucket(options.height);

    return do_preallocate_into_bucket(m_free, m_current_bytes,
                                      m_config.max_retained_bytes,
                                      options, rounded_w, rounded_h, options.count);
}

size_t FramebufferPool::ensure_preallocated(const FramebufferPoolPreallocOptions& options) {
    if (options.width <= 0 || options.height <= 0 || options.count == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const int rounded_w = round_up_bucket(options.width);
    const int rounded_h = round_up_bucket(options.height);
    FramebufferPoolKey key{rounded_w, rounded_h};

    size_t existing = 0;
    auto it = m_free.find(key);
    if (it != m_free.end()) {
        existing = it->second.size();
    }
    if (existing >= options.count) {
        return 0;
    }

    const size_t needed = options.count - existing;
    return do_preallocate_into_bucket(m_free, m_current_bytes,
                                      m_config.max_retained_bytes,
                                      options, rounded_w, rounded_h, needed);
}

size_t FramebufferPool::warm_up(const std::vector<FramebufferPoolPreallocOptions>& predictions) {
    size_t total = 0;
    for (const auto& opt : predictions) {
        total += preallocate(opt);
    }
    return total;
}

} // namespace chronon3d::cache
