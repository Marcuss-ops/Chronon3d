#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <cstdlib>
#include <limits>
#include <string>

namespace chronon3d {

void PoolFbDeleter::operator()(Framebuffer* fb) const noexcept {
    if (!fb) return;
    // ── Scratch cleanup: FB is borrowed from a TransformScratchBuffer ──
    // The cleanup callback (a std::function capturing a Handle RAII) will
    // be destroyed when the PoolFbDeleter itself goes out of scope (after
    // this function returns).  The Handle destructor restores the FB to
    // the owner's storage.  We do NOT delete the FB here — the
    // TransformScratchBuffer still owns it.
    // Takes precedence over all other modes.
    if (scratch_cleanup) {
        return; // scratch_cleanup (and its captured Handle) destroyed after return
    }
    // ── Renderer-owned FB: no-op — renderer manages lifetime explicitly ──
    // Used by ping-pong buffers owned by SoftwareRenderer via RendererBufferRing.
    // No pool release, no scratch restore, no delete.  The renderer is
    // responsible for cleanup on resolution change or destruction.
    if (owned_by_renderer) {
        return;
    }
    // ── Scratch slot: return the FB to the slot (cleared) instead of pool ──
    // This keeps a persistent buffer alive across frames without the
    // acquire/release cycle that causes pool bucket misses.
    if (scratch_slot) {
        fb->clear(Color::transparent());
        *scratch_slot = fb;
        return;
    }
    // Check that the pool is still alive before dereferencing.
    // pool_alive is a weak_ptr created from the pool's m_alive shared_ptr.
    // When the pool is destroyed, m_alive is set false and its shared_ptr is
    // released, making pool_alive.lock() return null.
    if (pool && pool_alive.lock()) {
        pool->release(fb);
    } else {
        delete fb;
    }
}

} // namespace chronon3d

namespace chronon3d::cache {

namespace {

size_t resolve_default_max_bytes(size_t fallback) {
    const char* env = std::getenv("CHRONON_FB_POOL_MAX_MB");
    if (!env || !*env) {
        return fallback;
    }
    std::string env_str(env);
    if (env_str == "illimitato" || env_str == "unlimited") {
        return std::numeric_limits<size_t>::max();
    }
    try {
        const size_t mb = static_cast<size_t>(std::stoull(env));
        if (mb == 0) {
            return fallback;
        }
        return mb * 1024ULL * 1024ULL;
    } catch (...) {
        spdlog::warn("CHRONON_FRAMEBUFFER_POOL_MAX_MB: invalid value '{}', using default", env);
        return fallback;
    }
}

// Round up to a fixed-size bucket with coarser granularity for larger sizes.
// The goal is to ensure that similar size requests (e.g. 602 vs 652 pixels)
// land in the SAME bucket, eliminating framebuffer pool size mismatches.
//
// Bucket scheme:
//      1 –   64 → 64-wide  (tiny intermediates / early-exit buffers)
//     65 –  256 → 128-wide (small effect inputs)
//    257 – 1024 → 256-wide (medium intermediate renders)
//   1025+        → 512-wide (full-frame buffers, stable at video resolution)
// Round up to a fixed-size bucket with finer granularity.
// This prevents massive memory bloat (e.g., 1080p rounding to 2048x1536, wasting 50% memory)
// while keeping similar sizes grouped together to maintain high cache hit rates.
int round_up_bucket(int val) {
    if (val <= 0) return 0;
    if (val <= 64) {
        return ((val + 7) / 8) * 8; // 8-aligned for micro-buffers
    } else if (val <= 256) {
        return ((val + 15) / 16) * 16; // 16-aligned
    } else if (val <= 1024) {
        return ((val + 127) / 128) * 128; // 128-aligned to collapse near-size buckets
    } else {
        return ((val + 127) / 128) * 128; // 128-aligned (full-frame buffers round close to exact resolution)
    }
}

} // namespace

FramebufferPool::FramebufferPool(size_t max_bytes)
    : m_max_bytes(resolve_default_max_bytes(max_bytes)),
      m_alive(std::make_shared<bool>(true)) {}

FramebufferPool::~FramebufferPool() {
    if (m_alive) {
        *m_alive = false;
    }
}

void FramebufferPool::set_arena(std::shared_ptr<FramebufferArena> arena) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_arena != arena) {
        m_arena = std::move(arena);
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

    const auto t0 = std::chrono::high_resolution_clock::now();
    bool fresh_alloc = false;
    auto fb = acquire_unique(width, height, &fresh_alloc);
    const auto t1 = std::chrono::high_resolution_clock::now();

    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_acquire_ms.fetch_add(
            static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t1 - t0).count()),
            std::memory_order_relaxed);
        // Note: pool counters are incremented inside acquire_unique()
        // (exact_hit, best_fit_reuse, empty_alloc) — not here.
    }

    // Skip clear when the FB was freshly allocated (constructor zero-initializes)
    // OR when the framebuffer is already known to be fully cleared to transparent.
    if (clear && fb && !fresh_alloc && !fb->is_content_cleared()) {
        const auto tc0 = std::chrono::high_resolution_clock::now();
        fb->clear(Color::transparent());
        const auto tc1 = std::chrono::high_resolution_clock::now();
        m_total_clears.fetch_add(1, std::memory_order_relaxed);
        if (profiling::g_current_counters) {
            const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(tc1 - tc0).count());
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
            auto fb = std::move(bucket.back());
            bucket.pop_back();
            m_current_bytes -= fb->size_bytes();
            m_total_reuses.fetch_add(1, std::memory_order_relaxed);
            if (profiling::g_current_counters) {
                profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
                profiling::g_current_counters->framebuffer_pool_exact_hit.fetch_add(1, std::memory_order_relaxed);
            }
            fb->resize_logical(width, height);
            // Pool FB: content is stale — caller must clear if needed.
            return fb;
        }
        // No exact match — will try best-fit or allocate fresh.
        // empty_alloc is incremented when a fresh allocation is made
        // (after best-fit scan also fails).
    }

    // Best-effort fallback: reuse a larger bucket when an exact bucket is not
    // available. This reduces fragmentation when different logical sizes land
    // in nearby rounded buckets, while still guaranteeing the buffer is at
    // least as large as the request after logical resize.
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
        auto fb = std::move(bucket.back());
        bucket.pop_back();
        m_current_bytes -= fb->size_bytes();
        m_total_reuses.fetch_add(1, std::memory_order_relaxed);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
            profiling::g_current_counters->framebuffer_pool_best_fit_reuse.fetch_add(1, std::memory_order_relaxed);
        }
        fb->resize_logical(width, height);
        // Best-fit pool FB: content is stale — caller must clear if needed.
        return fb;
    }

    if (m_arena) {
        void* ptr = m_arena->allocate(static_cast<size_t>(rounded_w) * rounded_h * sizeof(Color));
        if (ptr) {
            auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h, static_cast<Color*>(ptr));
            fb->resize_logical(width, height);
            // Arena memory is NOT zero-initialized — caller must clear.
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
    // Fresh allocation: Framebuffer constructor zero-initializes via
    // m_pixels.resize(..., Color::transparent()).  No clear needed.
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
    // Skip clear for fresh allocations (constructor zero-initializes)
    // or when the framebuffer is already fully cleared to transparent.
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
    // Skip clear for fresh allocations (constructor zero-initializes)
    // or when the framebuffer is already fully cleared to transparent.
    if (clear && fb && !fresh_alloc && !fb->is_content_cleared()) {
        fb->clear(Color::transparent());
    }
    return OwnedFB(fb.release(), PoolFbDeleter{this, m_alive});
}

OwnedFB FramebufferPool::acquire_owned_raw(int width, int height, bool clear) {
    bool fresh_alloc = false;
    auto fb = acquire_unique(width, height, &fresh_alloc);
    // Skip clear for fresh allocations (constructor zero-initializes)
    // or when the framebuffer is already fully cleared to transparent.
    if (clear && fb && !fresh_alloc && !fb->is_content_cleared()) {
        fb->clear(Color::transparent());
    }
    return OwnedFB(fb.release(), PoolFbDeleter{this, m_alive});
}

void FramebufferPool::release(Framebuffer* fb) {
    CHRONON_ZONE_C("framebuffer_release", trace_category::kPipeline);
    if (!fb) return;

    m_total_returns.fetch_add(1, std::memory_order_relaxed);

    std::unique_ptr<Framebuffer> owned(fb);

    // Arena-backed framebuffers are non-owning wrappers around arena memory.
    // They become stale when the arena is reset for the next frame, so they
    // must NOT be returned to the free list — just drop them.
    if (owned->is_arena_allocated()) {
        return; // wrapper destroyed by unique_ptr; pixels remain arena-owned
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Restore full allocated size so the Framebuffer is perfectly standardized in the pool
    const int alloc_w = owned->allocated_width();
    const int alloc_h = owned->allocated_height();
    owned->resize_logical(alloc_w, alloc_h);

    const size_t weight = owned->size_bytes();
    if (m_current_bytes + weight > m_max_bytes) {
        // Pool is full; drop the framebuffer
        return;
    }

    FramebufferPoolKey key{alloc_w, alloc_h};
    m_current_bytes += weight;
    m_free[key].push_back(std::move(owned));
    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_buffer_returned_to_pool_count.fetch_add(1, std::memory_order_relaxed);
    }
}

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

/// Shared allocation helper used by both preallocate() and ensure_preallocated().
/// Must be called while holding the pool mutex.
size_t do_preallocate_into_bucket(
    std::unordered_map<
        FramebufferPoolKey,
        std::vector<std::unique_ptr<Framebuffer>>,
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
        if (current_bytes + weight > max_bytes) {
            break;
        }

        current_bytes += weight;
        free_map[key].push_back(std::move(fb));
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

    return do_preallocate_into_bucket(m_free, m_current_bytes, m_max_bytes,
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
    return do_preallocate_into_bucket(m_free, m_current_bytes, m_max_bytes,
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
    return m_max_bytes;
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
        .max_bytes = m_max_bytes,
        .total_allocations = allocs,
        .total_reuses = reuses,
        .total_returns = m_total_returns.load(std::memory_order_relaxed),
        .total_clears = m_total_clears.load(std::memory_order_relaxed),
        .arena_bytes = m_arena ? m_arena->total_bytes() : 0,
        .hit_rate = total > 0 ? static_cast<double>(reuses) / static_cast<double>(total) : 0.0
    };
}

void FramebufferPool::reset_counters() {
    m_total_allocations.store(0, std::memory_order_relaxed);
    m_total_reuses.store(0, std::memory_order_relaxed);
    m_total_returns.store(0, std::memory_order_relaxed);
    m_total_clears.store(0, std::memory_order_relaxed);
}

} // namespace chronon3d::cache
