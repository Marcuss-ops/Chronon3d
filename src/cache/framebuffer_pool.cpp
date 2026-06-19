#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

namespace chronon3d {

void PoolFbDeleter::operator()(Framebuffer* fb) const noexcept {
    if (!fb) return;
    if (scratch_cleanup) {
        return;
    }
    if (owned_by_renderer) {
        return;
    }
    if (scratch_slot) {
        fb->clear(Color::transparent());
        *scratch_slot = fb;
        return;
    }
    if (auto pool = pool_weak.lock()) {
        pool->release(fb);
    } else {
        delete fb;
    }
}

} // namespace chronon3d

namespace chronon3d::cache {

namespace {

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

} // namespace

FramebufferPool::FramebufferPool(size_t max_bytes) {
    // 0 = use kDefaultBudgetBytes (384 MB).
    // The caller (SoftwareRenderer) pre-resolves Config/env overrides
    // and passes the resolved value.
    m_config.max_retained_bytes = (max_bytes > 0) ? max_bytes : kDefaultBudgetBytes;
}

FramebufferPool::FramebufferPool(const FramebufferPoolConfig& config)
    : m_config(config) {}

FramebufferPool::~FramebufferPool() = default;

void FramebufferPool::set_arena(std::shared_ptr<FramebufferArena> arena) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_arena != arena) {
        m_arena = std::move(arena);
    }
}

void FramebufferPool::set_budget_bytes(size_t max_retained_bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.max_retained_bytes = max_retained_bytes;
    // If the new budget is tighter, evict immediately.
    if (max_retained_bytes > 0) {
        while (m_current_bytes > max_retained_bytes) {
            if (!evict_global_lru()) break;
        }
    }
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire(int width, int height, bool clear) {
    return acquire_hinted(width, height, clear ? FramebufferAcquireHint::Default : FramebufferAcquireHint::ReuseNoClear);
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire_hinted(
    int width, int height,
    FramebufferAcquireHint hint)
{
    const bool clear = (hint == FramebufferAcquireHint::Default);

    const auto t0 = profiling::now();
    bool fresh_alloc = false;
    auto fb = acquire_unique(width, height, &fresh_alloc);
    const auto t1 = profiling::now();

    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_acquire_ms.fetch_add(
            static_cast<uint64_t>(profiling::duration_ms(t0, t1)),
            std::memory_order_relaxed);
    }

    if (clear && fb && !fresh_alloc && !fb->is_content_cleared()) {
        const auto tc0 = profiling::now();
        fb->clear(Color::transparent());
        const auto tc1 = profiling::now();
        m_total_clears.fetch_add(1, std::memory_order_relaxed);
        if (profiling::g_current_counters) {
            const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(tc0, tc1));
            profiling::g_current_counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
            profiling::g_current_counters->framebuffer_pool_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
        }
    }
    auto weak_pool = weak_from_this();
    return std::shared_ptr<Framebuffer>(fb.release(), [weak_pool](Framebuffer* ptr) {
        if (auto pool = weak_pool.lock()) {
            pool->release(ptr);
        } else {
            delete ptr;
        }
    });
}

std::unique_ptr<Framebuffer> FramebufferPool::acquire_unique(int width, int height, bool* out_fresh_alloc) {
    if (out_fresh_alloc) *out_fresh_alloc = false;

    std::lock_guard<std::mutex> lock(m_mutex);

    const int rounded_w = round_up_bucket(width);
    const int rounded_h = round_up_bucket(height);

    {
        FramebufferPoolKey key{rounded_w, rounded_h};
        auto it = m_free.find(key);
        if (it != m_free.end() && !it->second.empty()) {
            auto& bucket = it->second;
            // Take the oldest (LRU) entry from the front to keep fresher entries for later.
            auto entry = std::move(bucket.front());
            bucket.erase(bucket.begin());
            m_current_bytes -= entry.fb->size_bytes();
            m_total_reuses.fetch_add(1, std::memory_order_relaxed);
            if (profiling::g_current_counters) {
                profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
                profiling::g_current_counters->framebuffer_pool_exact_hit.fetch_add(1, std::memory_order_relaxed);
            }
            entry.fb->resize_logical(width, height);
            return std::move(entry.fb);
        }
    }

    auto best_fit = m_free.end();
    size_t best_area = std::numeric_limits<size_t>::max();
    for (auto it = m_free.begin(); it != m_free.end(); ++it) {
        const auto& key = it->first;
        if (key.width < rounded_w || key.height < rounded_h || it->second.empty()) {
            continue;
        }
        const size_t area = static_cast<size_t>(key.width) * static_cast<size_t>(key.height);
        if (area < best_area) {
            best_area = area;
            best_fit = it;
        }
    }

    if (best_fit != m_free.end()) {
        auto& bucket = best_fit->second;
        auto entry = std::move(bucket.back());
        bucket.pop_back();
        m_current_bytes -= entry.fb->size_bytes();
        m_total_reuses.fetch_add(1, std::memory_order_relaxed);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->framebuffer_pool_best_fit_reuse.fetch_add(1, std::memory_order_relaxed);
        }
        entry.fb->resize_logical(width, height);
        return std::move(entry.fb);
    }

    if (m_arena) {
        void* ptr = m_arena->allocate(static_cast<size_t>(rounded_w) * rounded_h * sizeof(Color));
        if (ptr) {
            auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h, static_cast<Color*>(ptr));
            fb->resize_logical(width, height);
            return fb;
        }
    }

    auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h);
    m_total_allocations.fetch_add(1, std::memory_order_relaxed);
    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_pool_empty_alloc.fetch_add(1, std::memory_order_relaxed);
        profiling::g_current_counters->framebuffer_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    fb->resize_logical(width, height);
    if (out_fresh_alloc) *out_fresh_alloc = true;
    return fb;
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire_pooled(int width, int height, std::shared_ptr<FramebufferPool> pool, bool clear) {
    CHRONON_ZONE_C("framebuffer_acquire", trace_category::kPipeline);
    if (!pool) {
        return acquire(width, height, clear);
    }

    bool fresh_alloc = false;
    auto fb = pool->acquire_unique(width, height, &fresh_alloc);
    if (clear && !fresh_alloc && !fb->is_content_cleared()) {
        fb->clear(Color::transparent());
    }
    std::weak_ptr<FramebufferPool> weak_pool = pool;
    return std::shared_ptr<Framebuffer>(fb.release(), [weak_pool](Framebuffer* ptr) {
        if (auto locked = weak_pool.lock()) {
            locked->release(ptr);
        } else {
            delete ptr;
        }
    });
}

OwnedFB FramebufferPool::acquire_owned(int width, int height, bool clear) {
    bool fresh_alloc = false;
    auto fb = acquire_unique(width, height, &fresh_alloc);
    if (clear && fb && !fresh_alloc && !fb->is_content_cleared()) {
        fb->clear(Color::transparent());
    }
    return OwnedFB(fb.release(), PoolFbDeleter{weak_from_this()});
}

// ─────────────────────────────────────────────────────────────────────────────
// Eviction helpers
// ─────────────────────────────────────────────────────────────────────────────

bool FramebufferPool::evict_one_from_bucket(FramebufferPoolKey key) {
    auto it = m_free.find(key);
    if (it == m_free.end() || it->second.empty()) return false;

    auto& bucket = it->second;
    // Find LRU entry in this bucket
    size_t lru_idx = 0;
    uint64_t min_tick = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < bucket.size(); ++i) {
        if (bucket[i].last_used_tick < min_tick) {
            min_tick = bucket[i].last_used_tick;
            lru_idx = i;
        }
    }

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

bool FramebufferPool::evict_global_lru() {
    std::optional<FramebufferPoolKey> lru_key;
    size_t lru_idx = 0;
    uint64_t min_tick = std::numeric_limits<uint64_t>::max();

    for (auto& [key, bucket] : m_free) {
        if (bucket.empty()) continue;
        for (size_t i = 0; i < bucket.size(); ++i) {
            if (bucket[i].last_used_tick < min_tick) {
                min_tick = bucket[i].last_used_tick;
                lru_idx = i;
                lru_key = key;
            }
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

bool FramebufferPool::evict_lru_for(size_t incoming_bytes) {
    if (m_config.max_retained_bytes == 0) return true; // unlimited

    bool evicted_any = false;
    while (m_current_bytes + incoming_bytes > m_config.max_retained_bytes) {
        if (!evict_global_lru()) break;
        evicted_any = true;
    }
    return evicted_any;
}

// ─────────────────────────────────────────────────────────────────────────────
// release() — with retention budget and per-size-class limit
// ─────────────────────────────────────────────────────────────────────────────

void FramebufferPool::release(Framebuffer* fb) {
    CHRONON_ZONE_C("framebuffer_release", trace_category::kPipeline);
    if (!fb) return;

    m_total_returns.fetch_add(1, std::memory_order_relaxed);

    std::unique_ptr<Framebuffer> owned(fb);

    // Arena-backed framebuffers are non-owning wrappers around arena memory.
    if (owned->is_arena_allocated()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Restore full allocated size so the Framebuffer is perfectly standardized in the pool
    const int alloc_w = owned->allocated_width();
    const int alloc_h = owned->allocated_height();
    owned->resize_logical(alloc_w, alloc_h);

    const size_t weight = owned->size_bytes();
    const uint64_t current_tick = ++m_tick;
    FramebufferPoolKey key{alloc_w, alloc_h};

    // Check the overall budget first — we may need to evict BEFORE inserting.
    // If the pool is over budget, evict LRU entries until there's room.
    if (m_config.max_retained_bytes > 0 && m_current_bytes + weight > m_config.max_retained_bytes) {
        evict_lru_for(weight);
    }

    // Check per-size-class limit.  If this bucket already has max entries,
    // evict the LRU entry in it to make room.
    auto it = m_free.find(key);
    if (it != m_free.end() && m_config.max_buffers_per_size_class > 0) {
        while (it->second.size() >= m_config.max_buffers_per_size_class) {
            if (!evict_one_from_bucket(key)) break;
        }
    }

    // If after eviction we still can't fit (no entries to evict), drop.
    if (m_config.max_retained_bytes > 0 && m_current_bytes + weight > m_config.max_retained_bytes) {
        // Can't fit — drop this framebuffer.
        m_evicted_count.fetch_add(1, std::memory_order_relaxed);
        m_evicted_bytes.fetch_add(weight, std::memory_order_relaxed);
        m_pressure_count.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Insert into pool
    m_current_bytes += weight;
    m_free[key].push_back(PoolEntry{std::move(owned), current_tick});

    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_buffer_returned_to_pool_count.fetch_add(1, std::memory_order_relaxed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pool management (unchanged logic, updated for PoolEntry)
// ─────────────────────────────────────────────────────────────────────────────

void FramebufferPool::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_free.clear();
    m_current_bytes = 0;
}

size_t FramebufferPool::current_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_bytes;
}

size_t FramebufferPool::available_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }
    return count;
}

namespace {

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

} // namespace

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

std::pair<int, int> FramebufferPool::round_to_bucket(int width, int height) {
    return {round_up_bucket(width), round_up_bucket(height)};
}

size_t FramebufferPool::warm_up(const std::vector<FramebufferPoolPreallocOptions>& predictions) {
    size_t total = 0;
    for (const auto& opt : predictions) {
        total += preallocate(opt);
    }
    return total;
}

size_t FramebufferPool::max_bytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.max_retained_bytes;
}

FramebufferPoolStats FramebufferPool::stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }

    const size_t allocs = m_total_allocations.load(std::memory_order_relaxed);
    const size_t reuses = m_total_reuses.load(std::memory_order_relaxed);
    const size_t total = allocs + reuses;

    return FramebufferPoolStats{
        .current_bytes = m_current_bytes,
        .available_count = count,
        .max_bytes = m_config.max_retained_bytes,
        .total_allocations = allocs,
        .total_reuses = reuses,
        .total_returns = m_total_returns.load(std::memory_order_relaxed),
        .total_clears = m_total_clears.load(std::memory_order_relaxed),
        .arena_bytes = m_arena ? m_arena->total_bytes() : 0,
        .hit_rate = total > 0 ? static_cast<double>(reuses) / static_cast<double>(total) : 0.0,
        .budget_bytes = m_config.max_retained_bytes,
        .retained_bytes = m_current_bytes,
        .evicted_count = m_evicted_count.load(std::memory_order_relaxed),
        .evicted_bytes = m_evicted_bytes.load(std::memory_order_relaxed),
        .pressure_count = m_pressure_count.load(std::memory_order_relaxed),
        .size_class_count = m_free.size(),
    };
}

void FramebufferPool::reset_counters() {
    m_total_allocations.store(0, std::memory_order_relaxed);
    m_total_reuses.store(0, std::memory_order_relaxed);
    m_total_returns.store(0, std::memory_order_relaxed);
    m_total_clears.store(0, std::memory_order_relaxed);
    m_evicted_count.store(0, std::memory_order_relaxed);
    m_evicted_bytes.store(0, std::memory_order_relaxed);
    m_pressure_count.store(0, std::memory_order_relaxed);
}

} // namespace chronon3d::cache
