#include <chronon3d/cache/framebuffer_pool.hpp>
#include "framebuffer_pool_lock_meter.hpp"
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
            m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(wait_t0), std::memory_order_relaxed);
        }
        detail::PoolLockHoldMeter hold(m_lock_hold_wall_us, contended);
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
        m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(wait_t0), std::memory_order_relaxed);
    }
    detail::PoolLockHoldMeter hold(m_lock_hold_wall_us, contended);
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
    m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    m_arena_set_count.fetch_add(1, std::memory_order_relaxed);
    m_arena_set_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    detail::PoolLockHoldMeter hold(m_lock_hold_wall_us);
    if (m_arena != arena) {
        m_arena = std::move(arena);
    }
}

void FramebufferPool::attach_arena_provider(
    std::function<FramebufferArena*()> provider) {
    const auto lock_t0 = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    // One arena-association event per job (Wave-4 acceptance: the pool's
    // arena association is job-lifetime, arena rotation is frame-lifetime).
    m_arena_set_count.fetch_add(1, std::memory_order_relaxed);
    m_arena_set_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    detail::PoolLockHoldMeter hold(m_lock_hold_wall_us);
    m_arena_provider = std::move(provider);
}

void FramebufferPool::detach_arena_provider() {
    const auto lock_t0 = std::chrono::steady_clock::now();
    std::lock_guard lock(m_mutex);
    m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
    detail::PoolLockHoldMeter hold(m_lock_hold_wall_us);
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
        m_lock_wait_us.fetch_add(detail::pool_lock_wait_us(lock_t0), std::memory_order_relaxed);
        detail::PoolLockHoldMeter hold(m_lock_hold_wall_us);

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
