// =============================================================================
// framebuffer_acquire_owned.inc — Owned framebuffer acquisition helpers for
// the RenderGraphContext facade.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/job/job_session.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace chronon3d::graph {

OwnedFB RenderGraphContext::acquire_owned_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    if (node_exec.terminal_video_encode_surface != runtime::kInvalidRenderSurfaceHandle) {
        const auto encode_surface = node_exec.terminal_video_encode_surface;
        node_exec.terminal_video_encode_surface = runtime::kInvalidRenderSurfaceHandle;
        node_exec.planned_physical_slot = nullptr;
        auto* fb = new Framebuffer(w, h, static_cast<Color*>(nullptr));
        fb->set_surface_handle(encode_surface);
        fb->mark_gpu_authoritative();
        if (bounds && (bounds->x0 != 0 || bounds->y0 != 0)) {
            fb->set_origin(bounds->x0, bounds->y0);
        }
        return OwnedFB(fb, PoolFbDeleter(DeleteFramebuffer{}));
    }

    // A compiler-assigned transient slot is the sole owner of this
    // framebuffer. Returning a RendererOwned view lets the node publish a
    // CachedFB without transferring or duplicating ownership; the slot is
    // reclaimed only when ExecutionState is destroyed. Interval coloring
    // guarantees that this pointer is not shared by overlapping resources.
    if (node_exec.planned_physical_slot) {
        OwnedFB& slot = *node_exec.planned_physical_slot;
        if (!slot) {
            auto* pool = framebuffer_pool.get();
            if (pool) {
                slot = pool->acquire_owned(w, h, clear);
            } else {
                slot = OwnedFB(new Framebuffer(w, h, clear), PoolFbDeleter(DeleteFramebuffer{}));
            }
        } else {
            slot->resize_logical(w, h);
        }
        slot->set_origin(0, 0);
        slot->set_opaque(false);
        slot->set_content_cleared(false);
        slot->set_key_digest(0);
        if (clear) {
            slot->clear(Color::transparent());
        }
        if (bounds) {
            slot->set_origin(bounds->x0, bounds->y0);
        }
        PoolFbDeleter renderer_owned;
        renderer_owned.policy = RendererOwned{};
        node_exec.planned_physical_slot = nullptr;
        return OwnedFB(slot.get(), std::move(renderer_owned));
    }

    OwnedFB out;
    auto* pool = framebuffer_pool.get();
    (void)bounds;
    (void)specific_clear_ms;
    if (pool) {
        out = pool->acquire_owned(w, h, clear);
    } else {
        out = OwnedFB(new Framebuffer(w, h, clear),
                      PoolFbDeleter(DeleteFramebuffer{}));
    }
    if (bounds && (bounds->x0 != 0 || bounds->y0 != 0)) {
        out->set_origin(bounds->x0, bounds->y0);
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(const Framebuffer& other) {
    if (node_exec.terminal_video_encode_surface != runtime::kInvalidRenderSurfaceHandle) {
        auto out = acquire_owned_fb(other.width(), other.height(), false);
        if (out && out->data() != other.data() && out->data() && other.data()) {
            FramebufferCopyAuthority::copy_rows(*out, other);
            out->set_origin(other.origin_x(), other.origin_y());
            out->set_opaque(other.is_opaque());
            out->set_content_cleared(other.is_content_cleared());
        }
        return out;
    }

    // F2.3 — the swap-transfer requires pool-provenance storage. Scratch,
    // arena and renderer-owned (planned physical slot) buffers must never be
    // re-wrapped with a pool deleter. Fall through to the explicit-copy path
    // instead of swapping.
    if (node_exec.reusable_bottom.get() == &other &&
        node_exec.reusable_bottom.use_count() <= 2 &&
        other.storage_transferable())
    {
        const auto orig_surface_handle = other.surface_handle();
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(const_cast<Framebuffer&>(other));
        placeholder->set_surface_handle(orig_surface_handle);

        PoolFbDeleter placeholder_deleter;
        if (framebuffer_pool) {
            placeholder_deleter = PoolFbDeleter{framebuffer_pool};
        }
        return OwnedFB(placeholder, std::move(placeholder_deleter));
    }

    if (node_exec.planned_physical_slot) {
        auto out = acquire_owned_fb(other.width(), other.height(), false);
        if (out && out->data() != other.data()) {
            FramebufferCopyAuthority::copy_rows(*out, other);
            out->set_origin(other.origin_x(), other.origin_y());
            out->set_opaque(other.is_opaque());
            out->set_content_cleared(other.is_content_cleared());
        }
        return out;
    }

    if (!other.data() && other.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        auto* fresh = new Framebuffer(other.width(), other.height(), false);
        fresh->set_surface_handle(other.surface_handle());
        fresh->set_origin(other.origin_x(), other.origin_y());
        fresh->set_opaque(other.is_opaque());
        fresh->set_content_cleared(other.is_content_cleared());
        if (!other.is_cpu_authoritative()) {
            fresh->mark_gpu_authoritative();
        }
        return OwnedFB(fresh, PoolFbDeleter(DeleteFramebuffer{}));
    }

    OwnedFB out;
    auto* pool = framebuffer_pool.get();
    if (pool) {
        out = pool->acquire_from(other);
    } else {
        auto* fresh = new Framebuffer(other.width(), other.height(), false);
        out = OwnedFB(fresh, PoolFbDeleter(DeleteFramebuffer{}));
        if (other.data()) {
            FramebufferCopyAuthority::copy_rows(*out, other);
        }
        out->set_surface_handle(other.surface_handle());
        if (!other.is_cpu_authoritative()) {
            out->mark_gpu_authoritative();
        }
    }
    return out;
}

OwnedFB RenderGraphContext::acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) {
    if (!src) return OwnedFB{};
    if (node_exec.terminal_video_encode_surface != runtime::kInvalidRenderSurfaceHandle) {
        auto out = acquire_owned_fb(src->width(), src->height(), false);
        if (out && out->data() != src->data() && out->data() && src->data()) {
            FramebufferCopyAuthority::copy_rows(*out, *src);
            out->set_origin(src->origin_x(), src->origin_y());
            out->set_opaque(src->is_opaque());
            out->set_content_cleared(src->is_content_cleared());
        }
        return out;
    }
    if (!src->data() && src->surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        auto* placeholder = new Framebuffer(src->width(), src->height(), false);
        placeholder->set_surface_handle(src->surface_handle());
        placeholder->set_origin(src->origin_x(), src->origin_y());
        placeholder->set_opaque(src->is_opaque());
        placeholder->set_content_cleared(src->is_content_cleared());
        if (!src->is_cpu_authoritative()) {
            placeholder->mark_gpu_authoritative();
        }
        return OwnedFB(placeholder, PoolFbDeleter(DeleteFramebuffer{}));
    }
    if (src.use_count() == 1 && src->storage_transferable()) {
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(*src);
        PoolFbDeleter placeholder_deleter;
        if (framebuffer_pool) {
            placeholder_deleter = PoolFbDeleter{framebuffer_pool};
        }
        return OwnedFB(placeholder, std::move(placeholder_deleter));
    }

    OwnedFB out;
    auto* pool = framebuffer_pool.get();
    if (pool) {
        out = pool->adopt_owned(std::move(src));
    } else {
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
    return acquire_owned_fb(w, h, clear, bounds);
}

CachedFB RenderGraphContext::own_to_cache(OwnedFB& owned, cache::FramebufferPool* pool) {
    if (!pool || !owned) {
        return CachedFB{};
    }
    return pool->cache_adopt(std::move(owned));
}

std::span<float> RenderGraphContext::download_scratch(int w, int h) {
    const std::size_t required = static_cast<std::size_t>(w) * h * 4;
    if (session) {
        return session->download_scratch(required);
    }
    if (fallback_download_scratch_.size() < required) {
        fallback_download_scratch_.resize(required);
    }
    return std::span<float>(fallback_download_scratch_.data(), required);
}

} // namespace chronon3d::graph
