#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include "framebuffer_pool_lock_meter.hpp"
#include <chrono>
#include <limits>

namespace chronon3d::cache {

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
        profiling::g_current_counters->framebuffer_acquire_wall_ms.fetch_add(
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
            profiling::g_current_counters->framebuffer_clear_wall_ms.fetch_add(elapsed, std::memory_order_relaxed);
            profiling::g_current_counters->framebuffer_pool_clear_wall_ms.fetch_add(elapsed, std::memory_order_relaxed);
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

// NOTE (FASE 18): round_up_bucket() moved to framebuffer_pool_evict.cpp.
// acquire_unique() uses the same rounding helper via the public static
// `round_to_bucket()` method.

std::unique_ptr<Framebuffer> FramebufferPool::acquire_unique(int width, int height, bool* out_fresh_alloc) {
    if (out_fresh_alloc) *out_fresh_alloc = false;

    std::unique_ptr<Framebuffer> reused;
    m_acquire_count.fetch_add(1, std::memory_order_relaxed);
    if (try_acquire_local(width, height, reused)) return reused;

    FramebufferArena* arena = nullptr;
    std::shared_ptr<FramebufferArena> arena_owner;
    const auto lock_t0 = std::chrono::steady_clock::now();
    auto [rounded_w, rounded_h] = round_to_bucket(width, height);
    {
        std::lock_guard lock(m_mutex);
        m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
        detail::PoolLockHoldMeter hold(m_lock_hold_wall_us);

        {
            FramebufferPoolKey key{rounded_w, rounded_h};
            auto it = m_free.find(key);
            if (it != m_free.end() && !it->second.empty()) {
                auto& bucket = it->second;
                // Take the oldest (LRU) entry from the front to keep fresher entries for later.
                auto entry = std::move(bucket.front());
                bucket.erase(bucket.begin());
                m_current_bytes -= entry.fb->size_bytes();
                release_class_slot(class_state_for(key, false));
                m_total_reuses.fetch_add(1, std::memory_order_relaxed);
                if (profiling::g_current_counters) {
                    profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
                    profiling::g_current_counters->framebuffer_pool_exact_hit.fetch_add(1, std::memory_order_relaxed);
                }
                reused = std::move(entry.fb);
                // OPT-POOL-BESTFIT — drop the bucket from the ordered index
                // when this acquire empties it (index holds non-empty buckets
                // only; re-inserted on the next release/preallocate).
                if (bucket.empty()) {
                    m_free.erase(it);
                    m_free_by_area.erase({free_bucket_area(key), key});
                }
            }
        }

        if (!reused) {
            // OPT-POOL-BESTFIT — best-fit via the ordered (area, dims) index:
            // lower_bound lands on the smallest-area bucket that still fits,
            // replacing the O(distinct-buckets) linear scan with an
            // O(log buckets) walk. Bucket refinement (the next entries with
            // the same area but larger missing dimension) is checked the
            // same way the previous scan did, by advancing while the entry
            // fits and has a smaller area than the current pick.
            auto candidate = m_free_by_area.lower_bound(
                {static_cast<std::uint64_t>(rounded_w) * static_cast<std::uint64_t>(rounded_h),
                 FramebufferPoolKey{rounded_w, rounded_h}});
            auto best_fit = m_free.end();
            std::uint64_t best_area = std::numeric_limits<std::uint64_t>::max();
            for (; candidate != m_free_by_area.end(); ++candidate) {
                const auto& key = candidate->second;
                if (key.width < rounded_w || key.height < rounded_h) {
                    continue;
                }
                const auto area = candidate->first;
                if (area < best_area) {
                    best_area = area;
                    best_fit = m_free.find(key);
                }
                // Ordered by area: the first fitting entry is the best fit
                // unless a same-area entry refines the missing dimension.
                // All same-area entries are contiguous, so keep scanning only
                // while the area equals the best found so far.
                if (area != best_area) break;
            }

            if (best_fit != m_free.end()) {
                auto& bucket = best_fit->second;
                auto entry = std::move(bucket.back());
                bucket.pop_back();
                m_current_bytes -= entry.fb->size_bytes();
                release_class_slot(class_state_for(best_fit->first, false));
                m_total_reuses.fetch_add(1, std::memory_order_relaxed);
                if (profiling::g_current_counters) {
                    profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
                    profiling::g_current_counters->framebuffer_pool_best_fit_reuse.fetch_add(1, std::memory_order_relaxed);
                }
                reused = std::move(entry.fb);
                if (bucket.empty()) {
                    const FramebufferPoolKey emptied = best_fit->first;
                    m_free.erase(best_fit);
                    m_free_by_area.erase({free_bucket_area(emptied), emptied});
                }
            }
        }

        if (!reused) {
            // The provider is intentionally sampled under the mutex because
            // it observes the job's rotating arena association. The actual
            // potentially expensive allocation happens after unlock.
            if (m_arena_provider) {
                arena = m_arena_provider();
            } else {
                arena_owner = m_arena;
                arena = arena_owner.get();
            }
        }
    }

    if (reused) {
        // Logical metadata is caller-local work and must not extend pool
        // mutex hold time. The storage still carries the pool return
        // contract, so a uniquely-owned borrowed wrapper may transfer it
        // zero-copy to another pool-owned wrapper.
        reused->resize_logical(width, height);
        reused->set_storage_transferable(true);
        return reused;
    }

    if (arena) {
        void* ptr = arena->allocate(static_cast<size_t>(rounded_w) * rounded_h * sizeof(Color));
        if (ptr) {
            auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h, static_cast<Color*>(ptr));
            fb->resize_logical(width, height);
            // P3 — an arena allocation is an allocation, not a reuse: the
            // caller's clear decision and the allocation telemetry must
            // classify it as fresh. Previously this path left the flag false,
            // so arena-backed buffers were counted as pool reuses (and
            // pointlessly cleared on the "reuse" path).
            if (out_fresh_alloc) *out_fresh_alloc = true;
            return fb;
        }
    }

    auto fb = std::make_unique<Framebuffer>(rounded_w, rounded_h);
    // Fresh heap-backed storage has the same pool return contract as reused
    // storage. Mark it transferable so a unique reusable input can follow
    // the zero-copy ownership path without a redundant allocation/copy.
    fb->set_storage_transferable(true);
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
    CHRONON_TRACE_SCOPE("chronon.pipeline", "framebuffer_acquire");
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
    return OwnedFB(fb.release(), PoolFbDeleter(ReturnToPool{weak_from_this()}));
}

OwnedFB FramebufferPool::acquire_owned_exact(int width, int height, bool clear) {
    // Tight projected text used to bypass the pool entirely, causing one
    // heap-backed framebuffer allocation per rendered frame.  The graph
    // preallocator already predicts the tight surface bucket, so use the
    // normal pooled path here.  The physical bucket may be slightly larger
    // than the logical text surface, but it is reused and avoids allocator
    // churn in the video loop.
    return acquire_owned(width, height, clear);
}

// ── acquire_noclear — hot-miss zero-fill-cost opt-in ────────────────
//
// Thin public wrapper around `acquire_unique()` that DOES NOT call
// `fb->clear(Color::transparent())` on a pool-miss reuse.  See the
// header doc-comment for the security contract — callers MUST
// overwrite every pixel or call `fb->clear(...)` themselves before
// reading pixels.  The existing `acquire` / `acquire_hinted` /
// `acquire_pooled` / `acquire_owned` keep their zero-on-return
// semantics, so callers who want a guaranteed-cleared buffer should
// continue using those.
std::unique_ptr<Framebuffer> FramebufferPool::acquire_noclear(int width, int height) {
    bool fresh_alloc_unused = false;
    return acquire_unique(width, height, &fresh_alloc_unused);
}

// ── TICKET-011-final — adapters restored after PoolFbDeleter migration

OwnedFB FramebufferPool::acquire_from(const Framebuffer& other) {
    bool fresh_alloc = false;
    auto fb = acquire_unique(other.width(), other.height(), &fresh_alloc);
    if (fb && other.data() && fb->data() && other.data() != fb->data()) {
        // F2.4 — single copy authority. Framebuffer storage is cache-line
        // padded and pool storage is size-class rounded: logical rows are
        // not contiguous at `width * sizeof(Color)`.  Copying the whole
        // logical area as one block shifts every row after the first when
        // the two strides differ, producing striped/corrupted effects.
        FramebufferCopyAuthority::copy_rows(*fb, other);
    }
    // Pixel storage alone is not the framebuffer contract. Cropped graph
    // nodes carry a canvas-space origin, and downstream compositors use it
    // to translate global clip/bbox coordinates back to local rows. Losing
    // it here relocates every pooled copy to (0,0), which is especially
    // visible after Transform/Lighting/Shadow chains.
    fb->set_origin(other.origin_x(), other.origin_y());
    fb->set_opaque(other.is_opaque());
    fb->set_content_cleared(other.is_content_cleared());
    fb->set_surface_handle(other.surface_handle());
    if (!other.is_cpu_authoritative()) {
        fb->mark_gpu_authoritative();
    }
    return OwnedFB(fb.release(), PoolFbDeleter(ReturnToPool{weak_from_this()}));
}

OwnedFB FramebufferPool::adopt_owned(std::shared_ptr<Framebuffer>&& src) {
    if (!src) return OwnedFB{};
    // std::shared_ptr has no .release(); deep-copy src into a fresh FB
    // owned by *this* pool (caller's src still holds a refcount on the
    // original).  Zero-copy alias goes through BorrowedCachedFB instead.
    auto* fresh = new Framebuffer(*src);
    return OwnedFB(fresh, PoolFbDeleter(ReturnToPool{weak_from_this()}));
}

CachedFB FramebufferPool::cache_adopt(OwnedFB owned) {
    return promote_to_cached(std::move(owned));
}

std::shared_ptr<Framebuffer> FramebufferPool::acquire_shared(int width, int height, bool clear) {
    return acquire_hinted(width, height, clear ? FramebufferAcquireHint::Default : FramebufferAcquireHint::ReuseNoClear);
}

// release_shared removed (TICKET-011 cleanup; zero callers).


} // namespace chronon3d::cache
