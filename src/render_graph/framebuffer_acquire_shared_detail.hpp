// =============================================================================
// framebuffer_acquire_shared.inc — Shared framebuffer acquisition helpers for
// RenderGraphContext. Kept separate from owned-buffer transfer semantics so
// each implementation fragment has one ownership model.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>

#include <algorithm>
#include <atomic>
#include <memory>

namespace chronon3d::graph {

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    std::shared_ptr<Framebuffer> out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        // Direct acquire_shared acquires a fresh CachedFB with pool-aware
        // deleter (no OwnedFB intermediary: acquire_shared(int,int,bool)
        // does not match OwnedFB).
        //
        // TICKET-012 follow-up: re-introduce a 5-arg overload of
        // FramebufferPool::acquire_shared that integrates `bounds` into
        // the ReturnToScratch policy slot routing and routes
        // specific_clear_ms into a per-call pool counter. For the
        // GREEN-build gate (3-arg pool overload only exists today), the
        // BBox+counter proxies are a no-op; the CachedFB path still
        // returns from pool with the default policy.
        (void)bounds;
        (void)specific_clear_ms;
        out = pool->acquire_shared(w, h, clear);
    } else {
        out = std::make_shared<Framebuffer>(w, h, clear);
    }
    return out;
}

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(
    const Framebuffer& other) const {
    // Zero-copy ownership transfer when the source is the uniquely-owned
    // reusable bottom input. A 1×1 placeholder swaps pixel storage with
    // the source; the placeholder is returned as a shared_ptr and will be
    // released back to the pool on destruction.
    if (node_exec.reusable_bottom.get() == &other &&
        node_exec.reusable_bottom.use_count() <= 2)
    {
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(const_cast<Framebuffer&>(other));
        std::weak_ptr<cache::FramebufferPool> weak_pool = services.framebuffer_pool;
        return std::shared_ptr<Framebuffer>(placeholder, [weak_pool](Framebuffer* ptr) noexcept {
            if (auto pool_ptr = weak_pool.lock()) {
                pool_ptr->release(ptr);
            } else {
                delete ptr;
            }
        });
    }

    std::shared_ptr<Framebuffer> out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        // Use acquire_shared for a correct pool reclaim path; copy pixels
        // because the pool's bucket may have a re-used allocation.
        out = pool->acquire_shared(other.width(), other.height(), false);
        if (out && out->data() != other.data()) {
            std::copy(other.data(),
                      other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                      out->data());
            if (node_exec.counters) {
                node_exec.counters->full_frame_copies.fetch_add(1, std::memory_order_relaxed);
                node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        out = std::make_shared<Framebuffer>(other.width(), other.height(), false);
        std::copy(other.data(),
                  other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                  out->data());
        if (node_exec.counters) {
            node_exec.counters->full_frame_copies.fetch_add(1, std::memory_order_relaxed);
            node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return out;
}

} // namespace chronon3d::graph
