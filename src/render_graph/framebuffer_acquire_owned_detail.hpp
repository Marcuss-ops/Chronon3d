// =============================================================================
// framebuffer_acquire_owned.inc — Owned framebuffer acquisition helpers for
// the RenderGraphContext facade.
// =============================================================================

#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>

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
    // A compiler-assigned transient slot is the sole owner of this
    // framebuffer. Returning a RendererOwned view lets the node publish a
    // CachedFB without transferring or duplicating ownership; the slot is
    // reclaimed only when ExecutionState is destroyed. Interval coloring
    // guarantees that this pointer is not shared by overlapping resources.
    if (node_exec.planned_physical_slot) {
        OwnedFB& slot = *node_exec.planned_physical_slot;
        if (!slot) {
            auto* pool = services.framebuffer_pool.get();
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
    auto* pool = services.framebuffer_pool.get();
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
    if (node_exec.reusable_bottom.get() == &other &&
        node_exec.reusable_bottom.use_count() <= 2)
    {
        const auto orig_surface_handle = other.surface_handle();
        auto* placeholder = new Framebuffer(1, 1, false);
        placeholder->swap_contents(const_cast<Framebuffer&>(other));
        placeholder->set_surface_handle(orig_surface_handle);

        PoolFbDeleter placeholder_deleter;
        if (services.framebuffer_pool) {
            placeholder_deleter = PoolFbDeleter{services.framebuffer_pool};
        }
        return OwnedFB(placeholder, std::move(placeholder_deleter));
    }

    if (node_exec.planned_physical_slot) {
        auto out = acquire_owned_fb(other.width(), other.height(), false);
        if (out && out->data() != other.data()) {
            const i32 copy_width = std::min(other.width(), out->width());
            const i32 copy_height = std::min(other.height(), out->height());
            const usize row_bytes = static_cast<usize>(copy_width) * sizeof(Color);
            for (i32 y = 0; y < copy_height; ++y) {
                std::memcpy(out->pixels_row(y), other.pixels_row(y), row_bytes);
            }
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
    auto* pool = services.framebuffer_pool.get();
    if (pool) {
        out = pool->acquire_from(other);
    } else {
        auto* fresh = new Framebuffer(other.width(), other.height(), false);
        out = OwnedFB(fresh, PoolFbDeleter(DeleteFramebuffer{}));
        if (other.data()) {
            const auto row_bytes = static_cast<std::size_t>(other.width()) * sizeof(Color);
            for (i32 y = 0; y < other.height(); ++y) {
                std::memcpy(out->pixels_row(y), other.pixels_row(y), row_bytes);
            }
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
    if (src.use_count() == 1) {
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

} // namespace chronon3d::graph
