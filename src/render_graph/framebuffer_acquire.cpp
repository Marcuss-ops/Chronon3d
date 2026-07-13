// =============================================================================
// framebuffer_acquire.cpp — Implementation of the RenderGraphContext facade
// methods (`acquire_owned_fb`, `acquire_framebuffer`, etc.).
//
// TICKET-010 — these methods forward into the new sub-context fields:
//   - ctx.services.framebuffer_pool     (was ctx.services.framebuffer_pool)
//   - ctx.node_exec.transform_scratch   (was ctx.node_exec.transform_scratch)
//   - ctx.node_exec.ping_write          (was ctx.node_exec.ping_write)
//   - ctx.node_exec.reusable_inputs     (was ctx.node_exec.reusable_inputs)
// `clone_for_node_execution()` skips the same hot-path vectors as before,
// now under `ctx.node_exec.{node_telemetry, layer_telemetry, dof_depth,
// early_exit_skip, reusable_inputs}`.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>   // F3.2 — thread_local_counters for std::copy instrumentation
#include <algorithm>
#include <atomic>

namespace chronon3d::graph {

OwnedFB RenderGraphContext::acquire_owned_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    OwnedFB out;
    auto* pool = services.framebuffer_pool.get();
    // TICKET-012 follow-up: re-introduce a 5-arg overload of
    // FramebufferPool::acquire_owned that integrates `bounds` into the
    // ReturnToScratch policy slot routing and routes specific_clear_ms
    // into a per-call pool counter.  For the GREEN-build gate (3-arg
    // pool overload only exists today), the BBox+counter proxies are a
    // no-op; the OwnedFB hot path still returns from pool with the
    // default policy.
    (void)bounds;
    (void)specific_clear_ms;
    if (pool) {
        out = pool->acquire_owned(w, h, clear);
    } else {
        out = OwnedFB(new Framebuffer(w, h, clear),
                      PoolFbDeleter(DeleteFramebuffer{}));
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(const Framebuffer& other) {
    // ── Zero-copy ownership transfer (composite hot-path) ─────────────
    // When `other` is the uniquely-owned CachedFB at
    // `node_exec.reusable_bottom` (use_count == 1, populated by
    // execute_single_node for the bottom input), transfer ownership via
    // a tiny 1×1 placeholder pixel swap:
    //   - placeholder (1×1) gets BIG pixel data from `other`
    //   - `other` gets TINY pixel data from placeholder
    //   - we return `placeholder` as OwnedFB (with BIG data) using the
    //     ORIGINAL PoolFbDeleter from the CachedFB (correct pool
    //     association); the CachedFB drops its reference → its deleter
    //     returns the (now tiny) FB to its original pool.
    //
    // Net effect: zero pixel copies; `other` returns a 1×1 placeholder
    // to the pool instead of an 8 MB alloc.  Replaces the ~8 MB
    // `pool->acquire_from(other) → memcpy` that previously dominated
    // `compositenode_acquire_ms` in chained composite layouts.
    if (node_exec.reusable_bottom.get() == &other &&
        node_exec.reusable_bottom.use_count() == 1)
    {
        // Allocate a tiny 1×1 placeholder on the heap, then swap contents
        // with `other`:
        //   - placeholder ↔ other: placeholder gets BIG pixel data,
        //     `other` gets the 1×1 placeholder buffer.
        //
        // const_cast is required because swap_contents takes a non-const
        // Framebuffer& (it swaps m_allocated_width/m_pixels/etc.).  We
        // own the underlying FB lifetime via the CachedFB (not via
        // `other`'s const-ref param), so dropping the const is safe.
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(const_cast<Framebuffer&>(other));

        // Wire the placeholder to the current pool so it can be returned
        // to the pool on destruction.  run_node will additionally swap
        // this for parent_pool's shared_from_this() when it constructs
        // the result CachedFB, so this deleter is really just defensive
        // for the unusual case where the OwnedFB destructs without prior
        // release().  The outer-if guard `if (!fb) return` ensures
        // PoolFbDeleter::operator()(nullptr) is safe regardless of
        // policy variant.
        PoolFbDeleter placeholder_deleter;
        if (services.framebuffer_pool) {
            placeholder_deleter = PoolFbDeleter{services.framebuffer_pool};
        }
        return OwnedFB(placeholder, std::move(placeholder_deleter));
    }

    OwnedFB out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = pool->acquire_from(other);
    } else {
        // OwnedFB = unique_ptr<Framebuffer, PoolFbDeleter>; cannot be
        // constructed from std::make_unique<Framebuffer>() which yields
        // unique_ptr<Framebuffer, default_delete>.  Build the FB
        // manually and pair with the no-pool deleter.
        auto* fresh = new Framebuffer(other.width(), other.height(), false);
        out = OwnedFB(fresh, PoolFbDeleter(DeleteFramebuffer{}));
        std::copy(other.data(),
                  other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                  out->data());
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) {
    if (src && src.use_count() == 1) {
        // Uniquely referenced source: perform a zero-copy ownership transfer
        // via a 1×1 placeholder swap. The placeholder assumes the source's
        // pixel storage; the source keeps a tiny 1×1 buffer that is cheap to
        // destroy. Works whether or not a pool is present — if a pool exists,
        // the placeholder returns to it on destruction; otherwise it is deleted
        // directly.
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(*src);
        PoolFbDeleter placeholder_deleter;
        if (services.framebuffer_pool) {
            placeholder_deleter = PoolFbDeleter{services.framebuffer_pool};
        }
        return OwnedFB(placeholder, std::move(placeholder_deleter));
    }

    OwnedFB out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = pool->adopt_owned(std::move(src));
    } else {
        // No pool and the source is shared: we must deep-copy because the
        // caller expects an independent OwnedFB. Use the helper that copies
        // pixels into a fresh allocation.
        out = make_owned_fb_from_shared(std::move(src));
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_scratch_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds
) const {
    // Mark the scratch as "in use" while the OwnedFB is alive; deleter
    // restores it.  Implementation is in software_renderer's buffer_ring +
    // transform_scratch_buffer integration; here we only expose the
    // facade.
    return acquire_owned_fb(w, h, clear, bounds);
}

CachedFB RenderGraphContext::own_to_cache(OwnedFB& owned, cache::FramebufferPool* pool) {
    if (!pool || !owned) {
        return CachedFB{};
    }
    return pool->cache_adopt(std::move(owned));
}

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
        // specific_clear_ms into a per-call pool counter.  For the
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

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(const Framebuffer& other) const {
    // Zero-copy ownership transfer when the source is the uniquely-owned
    // reusable bottom input. A 1×1 placeholder swaps pixel storage with
    // the source; the placeholder is returned as a shared_ptr and will be
    // released back to the pool on destruction.
    if (node_exec.reusable_bottom.get() == &other &&
        node_exec.reusable_bottom.use_count() == 1)
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
        // Previously did unique_ptr.release() into shared_ptr, which
        // leaks pool ownership when refcount hits zero.  Use
        // acquire_shared for a correct pool reclaim path; copy pixels
        // because the pool's bucket may have a re-used allocation.
        out = pool->acquire_shared(other.width(), other.height(), false);
        if (out && out->data() != other.data()) {
            std::copy(other.data(),
                      other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                      out->data());
            // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — explicit full-frame
            // std::copy from pool reuse path. This is the canonical
            // avoidable-copy site: when the pool returns a re-used
            // allocation with a different data ptr, the entire
            // framebuffer is memcpy'd. Increment BOTH counters here:
            //   - full_frame_copies (byte-level full-FB memcpy)
            //   - full_frame_passes  (every pixel touched)
            // Per-frame gate (B03 CinematicGlow1080p) depends on this
            // counter reaching full_frame_copies_per_frame == 0 in steady
            // state because the swap_contents placeholder pattern dominates
            // (use_count() == 1 path above).
            auto& tls = chronon3d::thread_local_counters();
            tls.full_frame_copies.fetch_add(1, std::memory_order_relaxed);
            tls.full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        out = std::make_shared<Framebuffer>(other.width(), other.height(), false);
        std::copy(other.data(),
                  other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                  out->data());
        // F3.2 — same increment as above for the no-pool fallback. The
        // no-pool path lacks the swap_contents optimization entirely, so
        // every call results in an std::copy (count for Gate 1 awareness).
        auto& tls = chronon3d::thread_local_counters();
        tls.full_frame_copies.fetch_add(1, std::memory_order_relaxed);
        tls.full_frame_passes.fetch_add(1, std::memory_order_relaxed);
    }
    return out;
}

RenderGraphContext RenderGraphContext::clone_for_node_execution() const {
    RenderGraphContext copy;
    // Frame + camera + policy: pure POD / small-string copy.
    copy.frame_input = frame_input;
    copy.policy      = policy;
    // Services: copy pointer fields + shared_ptr refcount bumps.
    copy.services    = services;
    // Node execution state: copy small POD (counters path, profiler ptr,
    // clip_rect), skip large vectors (`node_telemetry`, `layer_telemetry`,
    // `dof_depth`, `early_exit_skip`, `reusable_inputs`).  See header
    // comment for rationale (per-node copy overhead reduction).
    copy.node_exec.counters         = node_exec.counters;
    copy.node_exec.profiler         = node_exec.profiler;
    copy.node_exec.clip_rect        = node_exec.clip_rect;
    copy.node_exec.active_tile_clip = node_exec.active_tile_clip;
    copy.node_exec.dirty_rect       = node_exec.dirty_rect;
    // scratch views (transform_scratch, ping_write, reusable_inputs)
    // are intentionally default-initialised in the clone (no aliases to
    // the parent's per-node scratch state).

    // P0-1 — share the frame_error slot so nodes writing errors into
    // their cloned context are visible to the parent executor.
    copy.frame_error = frame_error;

    return copy;
}

std::string RenderGraphContext::resolve_asset(const std::string& relative_path) const {
    if (relative_path.empty()) return relative_path;
    if (!relative_path.empty() && relative_path[0] == '/') return relative_path;
    const auto& root = frame_input.assets_root;
    if (root.empty()) return relative_path;
    if (!root.empty() && root.back() == '/') {
        return root + relative_path;
    }
    return root + "/" + relative_path;
}

} // namespace chronon3d::graph
