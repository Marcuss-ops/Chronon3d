#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

namespace chronon3d {

namespace {
// A release policy that cannot reclaim the storage is a programming error that
// used to be invisible: the deleter returned and the framebuffer lived until
// the pool was trimmed. Count it so a wrong policy is observable instead of a
// silent leak. Kept allocation-free and noexcept: this runs from a noexcept
// destructor (including during unwinding).
inline void note_unexpected_release_policy() noexcept {
    auto* counters = profiling::g_current_counters;
    if (counters != nullptr) {
        counters->unexpected_pool_release_policy_count.fetch_add(
            1, std::memory_order_relaxed);
    }
}
} // namespace

void PoolFbDeleter::operator()(Framebuffer* fb) const noexcept {
    if (!fb) return;
    std::visit([fb](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, RestoreScratchHandle>) {
            // The handle carries the callback that restores the scratch slot.
            // Invoke it. An empty callback means the storage can never be
            // reclaimed: that is the unexpected-policy case, and deleting is
            // the only non-leaking fallback.
            if (p.cleanup) {
                p.cleanup();
            } else {
                note_unexpected_release_policy();
                delete fb;
            }
        } else if constexpr (std::is_same_v<T, RendererOwned>) {
            // Deliberate no-op: the renderer manages the planned physical slot.
            return;
        } else if constexpr (std::is_same_v<T, ReturnToScratch>) {
            if (p.slot) {
                fb->clear(Color::transparent());
                *p.slot = fb;
            } else {
                // No slot to restore into — this used to leak silently.
                note_unexpected_release_policy();
                delete fb;
            }
        } else if constexpr (std::is_same_v<T, ReturnToPool>) {
            if (auto pool = p.pool.lock()) {
                pool->release(fb);
            } else {
                delete fb;
            }
        } else {
            delete fb;  // DeleteFramebuffer
        }
    }, policy);
}

} // namespace chronon3d

namespace chronon3d::cache {

namespace {

// Pool and worker-slot contention meters. The WAIT time (measured before a
// lock is acquired) isolates contention from the work itself, and the HOLD
// time attributes the protected interval. The meter's destructor runs at
// every return, so locked scopes need no separate reporting path.
std::uint64_t pool_lock_wait_us(std::chrono::steady_clock::time_point t0) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count());
}

class PoolLockHoldMeter {
public:
    explicit PoolLockHoldMeter(
        std::atomic<std::uint64_t>& sink, bool enabled = true) noexcept
        : sink_(sink), enabled_(enabled) {
        if (enabled_) t0_ = std::chrono::steady_clock::now();
    }
    ~PoolLockHoldMeter() {
        if (!enabled_) return;
        sink_.fetch_add(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0_).count()),
            std::memory_order_relaxed);
    }
    PoolLockHoldMeter(const PoolLockHoldMeter&) = delete;
    PoolLockHoldMeter& operator=(const PoolLockHoldMeter&) = delete;

private:
    std::atomic<std::uint64_t>& sink_;
    bool enabled_{true};
    std::chrono::steady_clock::time_point t0_{};
};

} // namespace

// NOTE (FASE 18): the anonymous round_up_bucket() intra-pixel step helper
// and all eviction / preallocation / warm-up methods are now in
// framebuffer_pool_evict.cpp, included by the parent cache CMakeLists.

FramebufferPool::FramebufferPool(size_t max_bytes) {
    // 0 = use kDefaultBudgetBytes (384 MB).
    // The caller (SoftwareRenderer) pre-resolves Config/env overrides
    // and passes the resolved value.
    m_config.max_retained_bytes = (max_bytes > 0) ? max_bytes : kDefaultBudgetBytes;
    m_budget_bytes.store(m_config.max_retained_bytes, std::memory_order_relaxed);
}

FramebufferPool::FramebufferPool(const FramebufferPoolConfig& config)
    : m_config(config) {
    m_budget_bytes.store(m_config.max_retained_bytes, std::memory_order_relaxed);
}

FramebufferPool::~FramebufferPool() = default;

std::size_t FramebufferPool::local_slot_index() noexcept {
    static std::atomic<std::size_t> next_slot{0};
    thread_local const std::size_t slot =
        next_slot.fetch_add(1, std::memory_order_relaxed) % kLocalSlotCount;
    return slot;
}

std::shared_ptr<FramebufferPool::PoolClassState>
FramebufferPool::class_state_for(FramebufferPoolKey key, bool create) {
    {
        std::shared_lock lock(m_class_states_mutex);
        const auto it = m_class_states.find(key);
        if (it != m_class_states.end()) return it->second;
        if (!create) return {};
    }
    std::unique_lock lock(m_class_states_mutex);
    if (const auto it = m_class_states.find(key); it != m_class_states.end()) {
        return it->second;
    }
    if (m_class_states.size() >= 256) {
        for (auto it = m_class_states.begin(); it != m_class_states.end();) {
            if (it->second->available.load(std::memory_order_relaxed) == 0 &&
                it->second.use_count() == 1) {
                it = m_class_states.erase(it);
            } else {
                ++it;
            }
        }
    }
    auto state = std::make_shared<PoolClassState>();
    m_class_states.emplace(key, state);
    return state;
}

bool FramebufferPool::reserve_retained_bytes(std::size_t bytes) noexcept {
    auto current = m_current_bytes.load(std::memory_order_relaxed);
    for (;;) {
        const auto budget = m_budget_bytes.load(std::memory_order_acquire);
        if (budget != 0 && (current > budget || bytes > budget - current)) return false;
        if (m_current_bytes.compare_exchange_weak(
                current, current + bytes,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            const auto confirmed_budget = m_budget_bytes.load(std::memory_order_acquire);
            if (confirmed_budget == 0 || current + bytes <= confirmed_budget) {
                update_peak(m_peak_retained_bytes, current + bytes);
                return true;
            }
            m_current_bytes.fetch_sub(bytes, std::memory_order_acq_rel);
            return false;
        }
    }
}

bool FramebufferPool::reserve_class_slot(
    const std::shared_ptr<PoolClassState>& state) const noexcept {
    if (!state) return false;
    const auto limit = m_config.max_buffers_per_size_class;
    auto current = state->available.load(std::memory_order_relaxed);
    for (;;) {
        if (limit > 0 && current >= limit) return false;
        if (state->available.compare_exchange_weak(
                current, current + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return true;
        }
    }
}

void FramebufferPool::release_class_slot(
    const std::shared_ptr<PoolClassState>& state) noexcept {
    if (state) state->available.fetch_sub(1, std::memory_order_acq_rel);
}

void FramebufferPool::update_peak(
    std::atomic<std::size_t>& peak, std::size_t value) noexcept {
    auto current = peak.load(std::memory_order_relaxed);
    while (current < value && !peak.compare_exchange_weak(
        current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

bool FramebufferPool::try_acquire_local(
    int width, int height, std::unique_ptr<Framebuffer>& out) {
    const auto [rounded_w, rounded_h] = round_to_bucket(width, height);
    const FramebufferPoolKey key{rounded_w, rounded_h};
    const auto index = local_slot_index();
    const auto generation = m_local_generation.load(std::memory_order_acquire);
    auto consume_slot = [&](std::size_t slot_index, bool exact) {
        auto& slot = m_local_slots[slot_index];
        std::unique_lock lock(slot.mutex, std::try_to_lock);
        const bool contended = !lock.owns_lock();
        if (contended) {
            const auto wait_t0 = std::chrono::steady_clock::now();
            lock.lock();
            m_lock_wait_us.fetch_add(pool_lock_wait_us(wait_t0), std::memory_order_relaxed);
        }
        PoolLockHoldMeter hold(m_lock_hold_wall_us, contended);
        if (generation != m_local_generation.load(std::memory_order_acquire) ||
            slot.generation != generation || !slot.framebuffer) {
            return false;
        }
        const bool fits = slot.key.width >= rounded_w && slot.key.height >= rounded_h;
        if (exact ? !(slot.key == key) : !fits) return false;
        out = std::move(slot.framebuffer);
        if (slot.class_state) release_class_slot(slot.class_state);
        m_current_bytes.fetch_sub(out->size_bytes(), std::memory_order_acq_rel);
        m_total_reuses.fetch_add(1, std::memory_order_relaxed);
        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_reuses.fetch_add(1, std::memory_order_relaxed);
            if (exact) {
                profiling::g_current_counters->framebuffer_pool_exact_hit.fetch_add(1, std::memory_order_relaxed);
            } else {
                profiling::g_current_counters->framebuffer_pool_best_fit_reuse.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return true;
    };

    if (consume_slot(index, true)) {
        out->resize_logical(width, height);
        return true;
    }

    std::size_t best_slot = m_local_slots.size();
    std::uint64_t best_area = std::numeric_limits<std::uint64_t>::max();
    for (std::size_t i = 0; i < m_local_slots.size(); ++i) {
        if (i == index) continue;
        auto& candidate = m_local_slots[i];
        std::unique_lock candidate_lock(candidate.mutex, std::try_to_lock);
        if (!candidate_lock.owns_lock() || !candidate.framebuffer ||
            candidate.generation != generation ||
            candidate.key.width < rounded_w || candidate.key.height < rounded_h) {
            continue;
        }
        const auto area = FramebufferPool::free_bucket_area(candidate.key);
        if (area < best_area) {
            best_area = area;
            best_slot = i;
        }
    }
    if (best_slot < m_local_slots.size() && consume_slot(best_slot, false)) {
        out->resize_logical(width, height);
        return true;
    }
    return false;
}

bool FramebufferPool::try_release_local(
    std::unique_ptr<Framebuffer>& framebuffer) {
    if (!framebuffer || framebuffer->is_arena_allocated()) return false;
    const FramebufferPoolKey key{
        framebuffer->allocated_width(), framebuffer->allocated_height()};
    const auto index = local_slot_index();
    const auto generation = m_local_generation.load(std::memory_order_acquire);
    auto& slot = m_local_slots[index];
    std::unique_lock lock(slot.mutex, std::try_to_lock);
    const bool contended = !lock.owns_lock();
    if (contended) {
        const auto wait_t0 = std::chrono::steady_clock::now();
        lock.lock();
        m_lock_wait_us.fetch_add(pool_lock_wait_us(wait_t0), std::memory_order_relaxed);
    }
    PoolLockHoldMeter hold(m_lock_hold_wall_us, contended);
    if (generation != m_local_generation.load(std::memory_order_acquire) ||
        slot.framebuffer) {
        return false;
    }
    std::shared_ptr<PoolClassState> new_state;
    const bool same_class = slot.class_state && slot.key == key;
    if (!same_class) new_state = class_state_for(key, true);
    const auto& state = same_class ? slot.class_state : new_state;
    if (!reserve_class_slot(state)) return false;
    const auto weight = framebuffer->size_bytes();
    if (!reserve_retained_bytes(weight)) {
        release_class_slot(state);
        return false;
    }
    framebuffer->resize_logical(framebuffer->allocated_width(), framebuffer->allocated_height());
    framebuffer->set_storage_transferable(true);
    slot.key = key;
    if (!same_class) slot.class_state = std::move(new_state);
    slot.generation = generation;
    slot.last_used_tick = m_tick.fetch_add(1, std::memory_order_relaxed) + 1;
    slot.framebuffer = std::move(framebuffer);
    if (profiling::g_current_counters) {
        profiling::g_current_counters->framebuffer_buffer_returned_to_pool_count.fetch_add(
            1, std::memory_order_relaxed);
    }
    return true;
}

void FramebufferPool::set_arena(std::shared_ptr<FramebufferArena> arena) {
    const auto lock_t0 = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    m_lock_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    m_arena_set_count.fetch_add(1, std::memory_order_relaxed);
    m_arena_set_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    PoolLockHoldMeter hold(m_lock_hold_wall_us);
    if (m_arena != arena) {
        m_arena = std::move(arena);
    }
}

void FramebufferPool::attach_arena_provider(
    std::function<FramebufferArena*()> provider) {
    const auto lock_t0 = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    m_lock_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    // One arena-association event per job (Wave-4 acceptance: the pool's
    // arena association is job-lifetime, arena rotation is frame-lifetime).
    m_arena_set_count.fetch_add(1, std::memory_order_relaxed);
    m_arena_set_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    PoolLockHoldMeter hold(m_lock_hold_wall_us);
    m_arena_provider = std::move(provider);
}

void FramebufferPool::detach_arena_provider() {
    const auto lock_t0 = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    m_lock_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    PoolLockHoldMeter hold(m_lock_hold_wall_us);
    m_arena_provider = {};
}

void FramebufferPool::set_budget_bytes(size_t max_retained_bytes) {
    std::vector<std::unique_ptr<Framebuffer>> deferred_destruction;
    deferred_destruction.reserve(16);
    {
        std::lock_guard lock(m_mutex);
        m_config.max_retained_bytes = max_retained_bytes;
        m_budget_bytes.store(max_retained_bytes, std::memory_order_release);
        // If the new budget is tighter, evict immediately. Destroy the large
        // pixel allocations after releasing the pool mutex.
        if (max_retained_bytes > 0) {
            while (m_current_bytes > max_retained_bytes) {
                if (evict_global_lru(deferred_destruction)) continue;
                bool removed_local = false;
                for (auto& slot : m_local_slots) {
                    std::lock_guard slot_lock(slot.mutex);
                    if (!slot.framebuffer || m_current_bytes <= max_retained_bytes) continue;
                    const auto bytes = slot.framebuffer->size_bytes();
                    release_class_slot(slot.class_state);
                    deferred_destruction.push_back(std::move(slot.framebuffer));
                    slot.class_state.reset();
                    m_current_bytes.fetch_sub(bytes, std::memory_order_acq_rel);
                    m_evicted_count.fetch_add(1, std::memory_order_relaxed);
                    m_evicted_bytes.fetch_add(bytes, std::memory_order_relaxed);
                    m_pressure_count.fetch_add(1, std::memory_order_relaxed);
                    removed_local = true;
                }
                if (!removed_local) break;
            }
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
        m_lock_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
        PoolLockHoldMeter hold(m_lock_hold_wall_us);

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

// ─────────────────────────────────────────────────────────────────────────────
// release() — with retention budget and per-size-class limit
// (Eviction helpers have moved to framebuffer_pool_evict.cpp, FASE 18.)
// ─────────────────────────────────────────────────────────────────────────────

void FramebufferPool::release(Framebuffer* fb) {
    CHRONON_TRACE_SCOPE("chronon.pipeline", "framebuffer_release");
    if (!fb) return;

    // Snapshot the callback under the read side of the pool authority. The
    // backend cleanup itself must run outside the pool lock.
    std::function<void(Framebuffer&)> native_surface_releaser;
    {
        std::shared_lock lock(m_mutex);
        native_surface_releaser = m_native_surface_releaser;
    }
    if (native_surface_releaser) native_surface_releaser(*fb);

    m_total_returns.fetch_add(1, std::memory_order_relaxed);
    m_release_count.fetch_add(1, std::memory_order_relaxed);

    std::unique_ptr<Framebuffer> owned(fb);
    if (owned->is_arena_allocated()) return;

    owned->resize_logical(owned->allocated_width(), owned->allocated_height());
    if (try_release_local(owned)) return;

    std::vector<std::unique_ptr<Framebuffer>> deferred_destruction;
    deferred_destruction.reserve(16);
    {
        const auto lock_t0 = std::chrono::steady_clock::now();
        std::lock_guard lock(m_mutex);
        m_lock_wait_us.fetch_add(pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
        PoolLockHoldMeter hold(m_lock_hold_wall_us);

        const int alloc_w = owned->allocated_width();
        const int alloc_h = owned->allocated_height();
        owned->resize_logical(alloc_w, alloc_h);
        const size_t weight = owned->size_bytes();
        const uint64_t current_tick = ++m_tick;
        const FramebufferPoolKey key{alloc_w, alloc_h};
        auto class_state = class_state_for(key, true);

        if (m_config.max_retained_bytes > 0 &&
            m_current_bytes + weight > m_config.max_retained_bytes) {
            evict_lru_for(weight, deferred_destruction);
        }

        auto it = m_free.find(key);
        if (m_config.max_buffers_per_size_class > 0) {
            while (class_state->available.load(std::memory_order_acquire) >=
                   m_config.max_buffers_per_size_class) {
                if (!evict_one_from_bucket(key, deferred_destruction)) break;
                it = m_free.find(key);
                if (it == m_free.end()) break;
            }
        }

        // `owned` is declared before the lock scope, so a framebuffer that
        // cannot fit is also destroyed after unlocking.
        if (m_config.max_retained_bytes > 0 &&
            m_current_bytes + weight > m_config.max_retained_bytes) {
            m_evicted_count.fetch_add(1, std::memory_order_relaxed);
            m_evicted_bytes.fetch_add(weight, std::memory_order_relaxed);
            m_pressure_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const bool class_reserved = reserve_class_slot(class_state);
        const bool bytes_reserved = class_reserved && reserve_retained_bytes(weight);
        if (!bytes_reserved) {
            if (class_reserved) release_class_slot(class_state);
            m_evicted_count.fetch_add(1, std::memory_order_relaxed);
            m_evicted_bytes.fetch_add(weight, std::memory_order_relaxed);
            m_pressure_count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        owned->set_storage_transferable(true);
        const auto bucket_it = m_free.find(key);
        const bool bucket_was_empty = bucket_it == m_free.end() || bucket_it->second.empty();
        m_free[key].push_back(PoolEntry{std::move(owned), current_tick});
        if (bucket_was_empty) m_free_by_area.insert({free_bucket_area(key), key});

        if (profiling::g_current_counters) {
            profiling::g_current_counters->framebuffer_buffer_returned_to_pool_count.fetch_add(
                1, std::memory_order_relaxed);
        }
    }
}

void FramebufferPool::set_native_surface_releaser(
    std::function<void(Framebuffer&)> releaser) {
    std::lock_guard lock(m_mutex);
    m_native_surface_releaser = std::move(releaser);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pool management (unchanged logic, updated for PoolEntry)
// ─────────────────────────────────────────────────────────────────────────────

void FramebufferPool::clear() {
    m_local_generation.fetch_add(1, std::memory_order_acq_rel);
    decltype(m_free) retired;
    std::array<std::unique_ptr<Framebuffer>, kLocalSlotCount> retired_local{};
    {
        std::lock_guard lock(m_mutex);
        retired.swap(m_free);
        // OPT-POOL-BESTFIT — the index must mirror m_free (non-empty buckets).
        m_free_by_area.clear();
        for (std::size_t i = 0; i < m_local_slots.size(); ++i) {
            auto& slot = m_local_slots[i];
            std::lock_guard slot_lock(slot.mutex);
            if (slot.framebuffer) release_class_slot(slot.class_state);
            retired_local[i] = std::move(slot.framebuffer);
            slot.class_state.reset();
        }
        m_current_bytes.store(0, std::memory_order_release);
        {
            std::unique_lock class_lock(m_class_states_mutex);
            m_class_states.clear();
        }
    }
}

// P1-21: explicit post-job lifecycle hook.  Honors the configured
// `FramebufferPoolClearPolicy` instead of hardcoding a clear().  The
// CLI / job executor should call this at the end of each job.  This
// replaces the legacy implicit-clear-after-job pattern that destroyed
// warm state in batch workloads.
//
//   * KeepWarm            — no-op (LRU handles eviction).
//   * TrimAfterJob        — calls clear() (drops all pooled FBs).
//   * TrimOnMemoryPressure — no-op (LRU handles eviction via
//                              max_retained_bytes).
//
// The policy is read under the same lock as `clear()` to avoid a
// torn-read race on `m_clear_policy`; the read is a single word so
// the cost is negligible.
void FramebufferPool::trim_after_job() {
    FramebufferPoolClearPolicy policy;
    {
        std::lock_guard lock(m_mutex);
        policy = m_clear_policy;
    }
    switch (policy) {
        case FramebufferPoolClearPolicy::KeepWarm:
            // No-op: preserve warm state for next job (LRU will trim
            // when the budget fills).
            return;
        case FramebufferPoolClearPolicy::TrimAfterJob:
            // Drop all pooled FBs to release memory for the next job.
            clear();
            return;
        case FramebufferPoolClearPolicy::TrimOnMemoryPressure:
            // No-op: LRU eviction handles trimming when
            // max_retained_bytes is exceeded.
            return;
    }
}

size_t FramebufferPool::current_bytes() const {
    std::shared_lock lock(m_mutex);
    return m_current_bytes.load(std::memory_order_relaxed);
}

size_t FramebufferPool::available_count() const {
    std::shared_lock lock(m_mutex);
    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }
    for (const auto& slot : m_local_slots) {
        std::lock_guard slot_lock(slot.mutex);
        count += slot.framebuffer ? 1 : 0;
    }
    return count;
}

// NOTE (FASE 18): preallocate / ensure_preallocated / round_to_bucket /
// warm_up / do_preallocate_into_bucket moved to framebuffer_pool_evict.cpp.

size_t FramebufferPool::max_bytes() const {
    std::shared_lock lock(m_mutex);
    return m_config.max_retained_bytes;
}

FramebufferPoolStats FramebufferPool::stats() const {
    std::shared_lock lock(m_mutex);

    size_t count = 0;
    for (const auto& [key, bucket] : m_free) {
        count += bucket.size();
    }
    for (const auto& slot : m_local_slots) {
        std::lock_guard slot_lock(slot.mutex);
        count += slot.framebuffer ? 1 : 0;
    }

    std::size_t size_class_count = 0;
    {
        std::shared_lock classes_lock(m_class_states_mutex);
        for (const auto& [key, state] : m_class_states) {
            if (state->available.load(std::memory_order_relaxed) != 0) {
                ++size_class_count;
            }
        }
    }

    const size_t allocs = m_total_allocations.load(std::memory_order_relaxed);
    const size_t reuses = m_total_reuses.load(std::memory_order_relaxed);
    const size_t total = allocs + reuses;

    return FramebufferPoolStats{
        .current_bytes = m_current_bytes.load(std::memory_order_relaxed),
        .available_count = count,
        .max_bytes = m_config.max_retained_bytes,
        .total_allocations = allocs,
        .total_reuses = reuses,
        .total_returns = m_total_returns.load(std::memory_order_relaxed),
        .total_clears = m_total_clears.load(std::memory_order_relaxed),
        .arena_bytes = m_arena ? m_arena->total_bytes() : 0,
        .hit_rate = total > 0 ? static_cast<double>(reuses) / static_cast<double>(total) : 0.0,
        .budget_bytes = m_config.max_retained_bytes,
        .retained_bytes = m_current_bytes.load(std::memory_order_relaxed),
        .peak_retained_bytes = m_peak_retained_bytes.load(std::memory_order_relaxed),
        .evicted_count = m_evicted_count.load(std::memory_order_relaxed),
        .evicted_bytes = m_evicted_bytes.load(std::memory_order_relaxed),
        .pressure_count = m_pressure_count.load(std::memory_order_relaxed),
        .size_class_count = size_class_count,
        .lock_wait_us = m_lock_wait_us.load(std::memory_order_relaxed),
        .lock_hold_wall_us = m_lock_hold_wall_us.load(std::memory_order_relaxed),
        .acquire_count = m_acquire_count.load(std::memory_order_relaxed),
        .release_count = m_release_count.load(std::memory_order_relaxed),
        .arena_set_count = m_arena_set_count.load(std::memory_order_relaxed),
        .arena_set_wait_us = m_arena_set_wait_us.load(std::memory_order_relaxed),
    };
}

void FramebufferPool::reset_counters() {
    m_total_allocations.store(0, std::memory_order_relaxed);
    m_total_reuses.store(0, std::memory_order_relaxed);
    m_total_returns.store(0, std::memory_order_relaxed);
    m_total_clears.store(0, std::memory_order_relaxed);
    m_lock_wait_us.store(0, std::memory_order_relaxed);
    m_lock_hold_wall_us.store(0, std::memory_order_relaxed);
    m_acquire_count.store(0, std::memory_order_relaxed);
    m_release_count.store(0, std::memory_order_relaxed);
    m_arena_set_count.store(0, std::memory_order_relaxed);
    m_arena_set_wait_us.store(0, std::memory_order_relaxed);
    std::lock_guard lock(m_mutex);
    m_peak_retained_bytes.store(
        m_current_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_evicted_count.store(0, std::memory_order_relaxed);
    m_evicted_bytes.store(0, std::memory_order_relaxed);
    m_pressure_count.store(0, std::memory_order_relaxed);
}

} // namespace chronon3d::cache
