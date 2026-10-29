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

#include "nodes/native_surface.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace chronon3d::graph {

namespace {

bool copy_gpu_only_surface_to(
    RenderGraphContext& ctx, const Framebuffer& source, Framebuffer& destination) {
    const auto source_handle = source.surface_handle();
    const auto source_record = ctx.surface_registry
        ? ctx.surface_registry->lookup(source_handle)
        : std::nullopt;
    if (!ctx.backend || !ctx.surface_registry ||
        source_handle == runtime::kInvalidRenderSurfaceHandle ||
        source.is_cpu_authoritative() ||
        !source_record.has_value() ||
        !ctx.backend_surface_api()->is_native_surface_valid(source_handle)) {
        return false;
    }

    auto desc = source_record->desc;
    desc.width = static_cast<std::uint32_t>(destination.width());
    desc.height = static_cast<std::uint32_t>(destination.height());
    desc.bytes = desc.tight_bytes();
    desc.usage = runtime::ResourceUsage::Storage;
    desc.lifetime = runtime::LifetimeClass::FrameTransient;

    auto* api = ctx.backend_surface_api();
    auto destination_handle = destination.surface_handle();
    const auto destination_record = destination_handle != source_handle &&
                                    ctx.surface_registry
        ? ctx.surface_registry->lookup(destination_handle)
        : std::nullopt;
    const bool reusable_destination = destination_record.has_value() &&
        destination_record->desc.width == desc.width &&
        destination_record->desc.height == desc.height &&
        destination_record->desc.format == desc.format &&
        api->is_native_surface_valid(destination_handle);
    bool created_destination = false;
    if (!reusable_destination) {
        destination_handle = ctx.surface_registry->create(desc);
        if (destination_handle == runtime::kInvalidRenderSurfaceHandle) return false;
        const auto created = api->create_surface(destination_handle, desc);
        if (!created.ok()) {
            (void)ctx.surface_registry->release(destination_handle);
            return false;
        }
        created_destination = true;
    }

    if (!api->copy_surface(destination_handle, source_handle).ok()) {
        if (created_destination) {
            (void)api->release_surface(destination_handle);
            (void)ctx.surface_registry->release(destination_handle);
        }
        return false;
    }

    destination.set_surface_handle(destination_handle);
    destination.set_origin(source.origin_x(), source.origin_y());
    destination.set_opaque(source.is_opaque());
    destination.set_content_cleared(source.is_content_cleared());
    destination.mark_gpu_authoritative();
    return true;
}

OwnedFB clone_gpu_only_surface(RenderGraphContext& ctx, const Framebuffer& source) {
    auto* clone = new Framebuffer(
        source.width(), source.height(), static_cast<Color*>(nullptr));
    if (!copy_gpu_only_surface_to(ctx, source, *clone)) {
        delete clone;
        return {};
    }
    return OwnedFB(clone, PoolFbDeleter(DeleteFramebuffer{}));
}

} // namespace

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
            if (policy.require_native_gpu) {
                slot = OwnedFB(new Framebuffer(w, h, static_cast<Color*>(nullptr)),
                               PoolFbDeleter(DeleteFramebuffer{}));
            } else {
                auto* pool = framebuffer_pool.get();
                if (pool) {
                    slot = pool->acquire_owned(w, h, clear);
                } else {
                    slot = OwnedFB(new Framebuffer(w, h, clear), PoolFbDeleter(DeleteFramebuffer{}));
                }
            }
        } else if (w > slot->allocated_width() || h > slot->allocated_height()) {
            // Interval coloring can reuse one transient slot for resources of
            // different sizes. A physical slot starts at the first resource's
            // dimensions, so grow it on demand before exposing it to a later,
            // larger resource in the same execution plan.
            slot.reset();
            if (policy.require_native_gpu) {
                slot = OwnedFB(new Framebuffer(w, h, static_cast<Color*>(nullptr)),
                               PoolFbDeleter(DeleteFramebuffer{}));
            } else if (auto* pool = framebuffer_pool.get()) {
                slot = pool->acquire_owned(w, h, clear);
            } else {
                slot = OwnedFB(new Framebuffer(w, h, clear),
                               PoolFbDeleter(DeleteFramebuffer{}));
            }
        } else {
            slot->resize_logical(w, h);
        }
        slot->set_origin(0, 0);
        slot->set_opaque(false);
        slot->set_content_cleared(false);
        slot->set_key_digest(0);
        if (clear && !policy.require_native_gpu) {
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
    (void)bounds;
    (void)specific_clear_ms;
    if (policy.require_native_gpu) {
        out = OwnedFB(new Framebuffer(w, h, static_cast<Color*>(nullptr)),
                      PoolFbDeleter(DeleteFramebuffer{}));
    } else {
        auto* pool = framebuffer_pool.get();
        if (pool) {
            out = pool->acquire_owned(w, h, clear);
        } else {
            out = OwnedFB(new Framebuffer(w, h, clear),
                          PoolFbDeleter(DeleteFramebuffer{}));
        }
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
        if (!other.data() &&
            other.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
            if (!out || !copy_gpu_only_surface_to(*this, other, *out)) {
                return {};
            }
            return out;
        }
        if (out && out->data() && other.data() && out->data() != other.data()) {
            FramebufferCopyAuthority::copy_rows(*out, other);
            out->set_origin(other.origin_x(), other.origin_y());
            out->set_opaque(other.is_opaque());
            out->set_content_cleared(other.is_content_cleared());
            // Planned transient slots also carry passthrough values. A
            // GPU-only value has no CPU rows to copy, so it must be copied to
            // an independent surface before a later writer can recycle this
            // slot. Sharing the numeric handle is not ownership: detaching
            // that handle would invalidate the still-live input framebuffer.
            const auto source_handle = other.surface_handle();
            const bool source_is_current_native =
                source_handle != runtime::kInvalidRenderSurfaceHandle &&
                !other.is_cpu_authoritative() && backend && surface_registry &&
                backend_surface_api()->is_native_surface_valid(source_handle);
            if (source_is_current_native) {
                // A planned physical slot is a writable output. Never publish
                // a second framebuffer aliasing its input surface: downstream
                // native composites would then read and write the same image.
                // The terminal slot is treated the same way, with the copy
                // landing directly on its pre-bound NVENC surface.
                if (ensure_empty_native_surface(*this, *out)) {
                    const auto destination_handle = out->surface_handle();
                    if (destination_handle != source_handle) {
                        const auto copied = backend_surface_api()->copy_surface_auxiliary(
                            destination_handle, source_handle, std::nullopt);
                        if (!copied.ok()) {
                            spdlog::error(
                                "[native-residency] planned-slot passthrough copy failed: {}",
                                copied.error().message);
                            release_native_surface(*this, *out);
                            out->mark_cpu_authoritative();
                        }
                    }
                    if (std::getenv("CHRONON3D_PATH_DIAG")) {
                        spdlog::info(
                            "[native-residency] clone-without-slot source={} destination={} extent={}x{} copied={}",
                            source_handle, out->surface_handle(), other.width(),
                            other.height(), out->surface_handle() != source_handle);
                    }
                } else {
                    out->clear_surface_handle();
                    out->mark_cpu_authoritative();
                }
            } else {
                out->set_surface_handle(source_handle);
            }
            if (out->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
                !other.is_cpu_authoritative()) {
                out->mark_gpu_authoritative();
            } else if (other.is_cpu_authoritative()) {
                out->mark_cpu_authoritative();
            }
        }
        return out;
    }

    if (!other.data() && other.surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
        // GPU-only source: clone into a distinct native surface so the
        // frame-scoped source stays untouched and no output aliases its
        // handle. Fail closed instead of wrapping the same surface a second
        // time or racing out-of-frame teardown.
        return clone_gpu_only_surface(*this, other);
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
        // The pixel copy has independent CPU storage and no ownership of the
        // source's frame-scoped GPU surface.
        out->clear_surface_handle();
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
        return clone_gpu_only_surface(*this, *src);
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
