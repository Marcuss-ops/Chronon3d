#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/backends/software/transform_scratch_buffer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <cstring>
namespace chronon3d::graph {

OwnedFB RenderGraphContext::acquire_owned_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    OwnedFB fb;
    if (resources.framebuffer_pool) {
        fb = resources.framebuffer_pool->acquire_owned(w, h, clear);
    } else {
        fb = OwnedFB(new Framebuffer(w, h), PoolFbDeleter{});
        if (clear) fb->clear(Color::transparent());
    }
    if (bounds) {
        fb->set_origin(bounds->x0, bounds->y0);
    } else {
        fb->set_origin(0, 0);
    }
    if (clear && telemetry.counters) {
        telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
        const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
        telemetry.counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
    }
    (void)specific_clear_ms;
    return fb;
}

OwnedFB RenderGraphContext::acquire_scratch_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds
) const {
    // Use scratch buffer when available and large enough.
    if (scratch.transform_scratch.fb &&
        scratch.transform_scratch.fb->allocated_width() >= w &&
        scratch.transform_scratch.fb->allocated_height() >= h) {
        auto* sc = scratch.transform_scratch.fb;
        sc->resize_logical(w, h);
        if (bounds) {
            sc->set_origin(bounds->x0, bounds->y0);
        } else {
            sc->set_origin(0, 0);
        }
        if (clear) {
            sc->clear(Color::transparent());
        }
        // Mark the scratch as in-use — deleter will restore it.
        scratch.transform_scratch.fb = nullptr;
        PoolFbDeleter deleter;
        deleter.policy = ReturnToScratch{scratch.transform_scratch.slot};
        return OwnedFB(sc, std::move(deleter));
    }
    // Fall back to normal pool acquire.
    return acquire_owned_fb(w, h, clear, bounds);
}

OwnedFB RenderGraphContext::acquire_owned_fb(const Framebuffer& other) {
    auto it = std::find(scratch.reusable_inputs.begin(), scratch.reusable_inputs.end(), &other);
    if (it != scratch.reusable_inputs.end()) {
        scratch.reusable_inputs.erase(it);
        OwnedFB fb = acquire_owned_fb(other.width(), other.height(), false);
        fb->set_origin(other.origin_x(), other.origin_y());
        fb->swap_contents(const_cast<Framebuffer&>(other));
        return fb;
    }

    OwnedFB fb = acquire_owned_fb(other.width(), other.height(), false);
    fb->set_origin(other.origin_x(), other.origin_y());
    if (fb.get() == &other) {
        return fb;
    }
    const Color* src_base = other.data();
    Color* dst_base = fb->data();
    {
        const auto t0 = profiling::now();
        const int copy_h = other.height();
        const int copy_w = other.width();
        const int src_stride = other.stride();
        const int dst_stride = fb->stride();
        const size_t row_bytes = static_cast<size_t>(copy_w) * sizeof(Color);

        if (src_stride == copy_w && dst_stride == copy_w) {
            // Contiguous buffer — single memcpy is faster than row-by-row.
            // On DDR5 a 33 MB copy takes ~1ms instead of ~3ms with 1080
            // individual std::copy_n calls.
            std::memcpy(dst_base, src_base, row_bytes * static_cast<size_t>(copy_h));
        } else if (static_cast<int64_t>(copy_h) * copy_w >= (int64_t{1} << 18) && copy_h >= 8) {
            // Large copy with padding: parallelize per row.
            if (telemetry.counters) {
                telemetry.counters->framebuffer_copy_parallel_calls.fetch_add(1, std::memory_order_relaxed);
            }
            tbb::parallel_for(
                tbb::blocked_range<int>(0, copy_h, 64),
                [&](const tbb::blocked_range<int>& range) {
                    for (int y = range.begin(); y < range.end(); ++y) {
                        std::memcpy(
                            dst_base + static_cast<size_t>(y) * dst_stride,
                            src_base + static_cast<size_t>(y) * src_stride,
                            row_bytes);
                    }
                });
        } else {
            for (i32 y = 0; y < copy_h; ++y) {
                std::memcpy(
                    dst_base + static_cast<size_t>(y) * dst_stride,
                    src_base + static_cast<size_t>(y) * src_stride,
                    row_bytes);
            }
        }
        if (telemetry.counters) {
            const auto elapsed = static_cast<uint64_t>(
                profiling::elapsed_us(t0) / 1000.0);
            telemetry.counters->framebuffer_copy_ms.fetch_add(elapsed, std::memory_order_relaxed);
        }
    }
    fb->set_opaque(other.is_opaque());
    fb->set_key_digest(other.key_digest());
    fb->set_content_cleared(other.is_content_cleared());
    return fb;
}

OwnedFB RenderGraphContext::acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) {
    if (!src) return nullptr;
    if (src.use_count() != 1) return acquire_owned_fb(*src);

    OwnedFB result = acquire_owned_fb(src->width(), src->height(), false);
    if (!result) return acquire_owned_fb(*src);

    // Swap pixel storage with the source — no pixel copy.
    result->set_origin(src->origin_x(), src->origin_y());
    result->set_opaque(src->is_opaque());
    result->swap_contents(*src);
    src.reset();
    return result;
}

CachedFB RenderGraphContext::own_to_cache(OwnedFB& owned, cache::FramebufferPool* pool) {
    if (!owned) return nullptr;
    Framebuffer* raw = owned.release();
    if (pool) {
        return CachedFB(raw, PoolFbDeleter{pool->shared_from_this()});
    }
    return CachedFB(raw, PoolFbDeleter{});
}

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    auto resolve_clear_clip = [&](Framebuffer& fb) -> std::optional<raster::BBox> {
        if (!clear) {
            return std::nullopt;
        }

        if (!options.dirty_rects_enabled) {
            return std::nullopt;
        }

        std::optional<raster::BBox> local_clip = tile.clip_rect;
        if (local_clip) {
            local_clip->x0 -= fb.origin_x();
            local_clip->x1 -= fb.origin_x();
            local_clip->y0 -= fb.origin_y();
            local_clip->y1 -= fb.origin_y();
            local_clip->clip_to(w, h);
            // Keep it empty if it is empty; fb->clear will return early and avoid clearing anything.
        }
        return local_clip;
    };

    if (resources.framebuffer_pool) {
        auto fb = resources.framebuffer_pool->acquire_pooled(w, h, resources.framebuffer_pool, false);
        if (bounds) {
            fb->set_origin(bounds->x0, bounds->y0);
        } else {
            fb->set_origin(0, 0);
        }
        if (clear) {
            const auto local_clip = resolve_clear_clip(*fb);
            const auto t_clr0 = profiling::now();
            fb->clear(Color::transparent(), local_clip);
            const auto t_clr1 = profiling::now();
            if (telemetry.counters) {
                const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(t_clr0, t_clr1));
                telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                if (specific_clear_ms) {
                    specific_clear_ms->fetch_add(elapsed, std::memory_order_relaxed);
                }
                
                telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t pixels = local_clip
                    ? static_cast<uint64_t>(std::max(0, local_clip->x1 - local_clip->x0)) *
                      static_cast<uint64_t>(std::max(0, local_clip->y1 - local_clip->y0))
                    : static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
                telemetry.counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
            }
        }
        return fb;
    }
    auto fb = std::make_shared<Framebuffer>(w, h);
    if (bounds) {
        fb->set_origin(bounds->x0, bounds->y0);
    }
    if (clear) {
        const auto local_clip = resolve_clear_clip(*fb);
        fb->clear(Color::transparent(), local_clip);
        if (telemetry.counters) {
            telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
            const uint64_t pixels = local_clip
                ? static_cast<uint64_t>(std::max(0, local_clip->x1 - local_clip->x0)) *
                  static_cast<uint64_t>(std::max(0, local_clip->y1 - local_clip->y0))
                : static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
            telemetry.counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
        }
    }
    return fb;
}

std::shared_ptr<Framebuffer> RenderGraphContext::acquire_framebuffer(const Framebuffer& other) const {
    auto fb = acquire_framebuffer(other.width(), other.height(), false);
    fb->set_origin(other.origin_x(), other.origin_y());
    if (fb.get() == &other) {
        return fb;
    }
    {
        const auto t0 = profiling::now();
        const Color* src_base = other.data();
        Color* dst_base = fb->data();
        const int copy_h = other.height();
        const int copy_w = other.width();
        const int src_stride = other.stride();
        const int dst_stride = fb->stride();
        const size_t row_bytes = static_cast<size_t>(copy_w) * sizeof(Color);

        if (src_stride == copy_w && dst_stride == copy_w) {
            std::memcpy(dst_base, src_base, row_bytes * static_cast<size_t>(copy_h));
        } else if (static_cast<int64_t>(copy_h) * copy_w >= (int64_t{1} << 18) && copy_h >= 8) {
            if (telemetry.counters) {
                telemetry.counters->framebuffer_copy_parallel_calls.fetch_add(1, std::memory_order_relaxed);
            }
            tbb::parallel_for(
                tbb::blocked_range<int>(0, copy_h, 64),
                [&](const tbb::blocked_range<int>& range) {
                    for (int y = range.begin(); y < range.end(); ++y) {
                        std::memcpy(
                            dst_base + static_cast<size_t>(y) * dst_stride,
                            src_base + static_cast<size_t>(y) * src_stride,
                            row_bytes);
                    }
                });
        } else {
            for (i32 y = 0; y < copy_h; ++y) {
                std::memcpy(
                    dst_base + static_cast<size_t>(y) * dst_stride,
                    src_base + static_cast<size_t>(y) * src_stride,
                    row_bytes);
            }
        }
        if (telemetry.counters) {
            const auto elapsed = static_cast<uint64_t>(
                profiling::elapsed_us(t0) / 1000.0);
            telemetry.counters->framebuffer_copy_ms.fetch_add(elapsed, std::memory_order_relaxed);
        }
    }
    fb->set_opaque(other.is_opaque());
    fb->set_key_digest(other.key_digest());
    fb->set_content_cleared(other.is_content_cleared());
    return fb;
}

RenderGraphContext RenderGraphContext::clone_for_node_execution() const {
    RenderGraphContext copy;
    // ── Sub-struct copies (cheap — POD + shared_ptr) ───────────────────
    copy.frame                      = frame;
    copy.camera                     = camera;
    copy.resources                  = resources;
    copy.options                    = options;
    copy.telemetry.counters         = telemetry.counters;
    copy.telemetry.profiler         = telemetry.profiler;
    copy.tile.dirty_rect            = tile.dirty_rect;
    copy.tile.clip_rect             = tile.clip_rect;
    copy.tile.tile_size             = tile.tile_size;
    copy.tile.tile_execution_enabled = tile.tile_execution_enabled;
    copy.tile.active_tile_clip      = tile.active_tile_clip;
    // When tile execution is enabled, don't copy the scratch buffer pointers —
    // each tile falls back to pool allocation instead.  This avoids data races
    // when multiple tiles execute transform nodes in parallel on the same
    // scratch slot (FramebufferSlotView contains raw pointers; copying them
    // would let multiple tiles simultaneously acquire and mutate the same buffer).
    // In non-tile mode the scratch buffer is safe (single-threaded) and remains
    // available as an optimization.
    if (tile.tile_execution_enabled) {
        copy.scratch.transform_scratch = {};
        copy.scratch.ping_write          = {};
    } else {
        copy.scratch.transform_scratch = scratch.transform_scratch;
        copy.scratch.ping_write          = scratch.ping_write;
    }

    // ── Vectors — selectively copy only what node.execute() may read ────
    // • early_exit_skip:  checked against the *parent* ctx before the copy
    // • node_telemetry / layer_telemetry: written via global telemetry
    //   paths, never read during node.execute()
    // • dof_depth: only needed when DOF tracking is active (~8 MB @ 1080p)
    if (options.track_dof_depth && !telemetry.dof_depth.empty()) {
        copy.telemetry.dof_depth = telemetry.dof_depth;
    }
    // reusable_inputs starts empty — populated per-node after the clone.

    return copy;
}

std::string RenderGraphContext::resolve_asset(const std::string& relative_path) const {
    return resolve_asset_path(frame.assets_root, relative_path);
}

} // namespace chronon3d::graph
