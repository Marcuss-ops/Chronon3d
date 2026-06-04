#include <chronon3d/render_graph/framebuffer_acquire.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chrono>

namespace chronon3d::graph {

OwnedFB RenderGraphContext::acquire_owned_fb(
    int w,
    int h,
    bool clear,
    std::optional<raster::BBox> bounds,
    std::atomic<uint64_t>* specific_clear_ms
) const {
    OwnedFB fb;
    if (framebuffer_pool) {
        fb = framebuffer_pool->acquire_owned(w, h, clear);
    } else {
        fb = OwnedFB(new Framebuffer(w, h), PoolFbDeleter{nullptr});
        if (clear) fb->clear(Color::transparent());
    }
    if (bounds) {
        fb->set_origin(bounds->x0, bounds->y0);
    } else {
        fb->set_origin(0, 0);
    }
    if (clear && counters) {
        counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
        const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
        counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
    }
    (void)specific_clear_ms;
    return fb;
}

OwnedFB RenderGraphContext::acquire_owned_fb(const Framebuffer& other) const {
    auto it = std::find(reusable_inputs.begin(), reusable_inputs.end(), const_cast<Framebuffer*>(&other));
    if (it != reusable_inputs.end()) {
        reusable_inputs.erase(it);
        OwnedFB fb = acquire_owned_fb(other.width(), other.height(), false);
        fb->set_origin(other.origin_x(), other.origin_y());
        fb->swap_contents(*const_cast<Framebuffer*>(&other));
        return fb;
    }

    OwnedFB fb = acquire_owned_fb(other.width(), other.height(), false);
    fb->set_origin(other.origin_x(), other.origin_y());
    if (fb.get() == &other) {
        return fb;
    }
    for (i32 y = 0; y < other.height(); ++y) {
        std::copy_n(other.pixels_row(y), other.width(), fb->pixels_row(y));
    }
    fb->set_opaque(other.is_opaque());
    fb->set_key_digest(other.key_digest());
    return fb;
}

OwnedFB RenderGraphContext::acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) const {
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
    return CachedFB(raw, PoolFbDeleter{pool});
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

        if (!dirty_rects_enabled) {
            return std::nullopt;
        }

        std::optional<raster::BBox> local_clip = clip_rect;
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

    if (framebuffer_pool) {
        auto fb = framebuffer_pool->acquire_pooled(w, h, framebuffer_pool, false);
        if (bounds) {
            fb->set_origin(bounds->x0, bounds->y0);
        } else {
            fb->set_origin(0, 0);
        }
        if (clear) {
            const auto local_clip = resolve_clear_clip(*fb);
            const auto t_clr0 = std::chrono::high_resolution_clock::now();
            fb->clear(Color::transparent(), local_clip);
            const auto t_clr1 = std::chrono::high_resolution_clock::now();
            if (counters) {
                const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_clr1 - t_clr0).count());
                counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                if (specific_clear_ms) {
                    specific_clear_ms->fetch_add(elapsed, std::memory_order_relaxed);
                }
                
                counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                const uint64_t pixels = local_clip
                    ? static_cast<uint64_t>(std::max(0, local_clip->x1 - local_clip->x0)) *
                      static_cast<uint64_t>(std::max(0, local_clip->y1 - local_clip->y0))
                    : static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
                counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
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
        if (counters) {
            counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
            const uint64_t pixels = local_clip
                ? static_cast<uint64_t>(std::max(0, local_clip->x1 - local_clip->x0)) *
                  static_cast<uint64_t>(std::max(0, local_clip->y1 - local_clip->y0))
                : static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
            counters->clear_pixels.fetch_add(pixels, std::memory_order_relaxed);
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
    for (i32 y = 0; y < other.height(); ++y) {
        std::copy_n(other.pixels_row(y), other.width(), fb->pixels_row(y));
    }
    fb->set_opaque(other.is_opaque());
    fb->set_key_digest(other.key_digest());
    return fb;
}

} // namespace chronon3d::graph
