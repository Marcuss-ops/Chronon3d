// =============================================================================
// framebuffer_acquire.cpp — Implementation of the RenderGraphContext facade
// methods (`acquire_owned_fb`, `acquire_framebuffer`, etc.).
//
// TICKET-010 — these methods forward into the new sub-context fields:
//   - ctx.services.framebuffer_pool     (was ctx.resources.framebuffer_pool)
//   - ctx.node_exec.transform_scratch   (was ctx.scratch.transform_scratch)
//   - ctx.node_exec.ping_write          (was ctx.scratch.ping_write)
//   - ctx.node_exec.reusable_inputs     (was ctx.scratch.reusable_inputs)
// `clone_for_node_execution()` skips the same hot-path vectors as before,
// now under `ctx.node_exec.{node_telemetry, layer_telemetry, dof_depth,
// early_exit_skip, reusable_inputs}`.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
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
    if (pool) {
        out = pool->acquire_owned(w, h, clear, bounds, specific_clear_ms);
    } else {
        out = OwnedFB(std::make_unique<Framebuffer>(w, h, clear));
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(const Framebuffer& other) {
    OwnedFB out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = pool->acquire_from(other);
    } else {
        out = OwnedFB(std::make_unique<Framebuffer>(other.width(), other.height(), false));
        std::copy(other.data(),
                  other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                  out->data());
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) {
    OwnedFB out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = pool->adopt_owned(std::move(src));
    } else {
        // No pool — adopt as a plain unique_ptr with no-op deleter.
        out = OwnedFB(OwnedFB::from_shared_no_pool(std::move(src)));
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
        out = pool->acquire_shared(std::move(OwnedFB(acquire_owned_fb(w, h, clear, bounds, specific_clear_ms))));
    } else {
        out = std::make_shared<Framebuffer>(w, h, clear);
    }
    return out;
}

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(const Framebuffer& other) const {
    std::shared_ptr<Framebuffer> out;
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = std::shared_ptr<Framebuffer>(pool->acquire_from(other).release());
    } else {
        out = std::make_shared<Framebuffer>(other.width(), other.height(), false);
        std::copy(other.data(),
                  other.data() + static_cast<size_t>(other.width()) * static_cast<size_t>(other.height()),
                  out->data());
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
