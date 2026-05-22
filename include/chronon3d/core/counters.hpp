#pragma once
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <atomic>
#include <array>
#include <cstdint>

namespace chronon3d {

struct RenderCounters {
    std::atomic<uint64_t> pixels_touched{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> nodes_executed{0};
    std::atomic<uint64_t> layers_rendered{0};
    std::atomic<uint64_t> text_glyphs_rasterized{0};
    std::atomic<uint64_t> images_sampled{0};
    std::atomic<uint64_t> blur_pixels{0};
    std::atomic<uint64_t> simd_lerp_calls{0};
    std::atomic<uint64_t> tiles_total{0};
    std::atomic<uint64_t> tiles_hit{0};
    std::atomic<uint64_t> tiles_miss{0};
    std::atomic<uint64_t> tiles_partial{0};
    std::atomic<uint64_t> node_cache_hash_collisions{0};

    std::atomic<uint64_t> clear_calls{0};
    std::atomic<uint64_t> clear_pixels{0};
    std::atomic<uint64_t> clear_copy_pixels{0};
    std::atomic<uint64_t> composite_calls{0};
    std::atomic<uint64_t> composite_pixels{0};
    std::atomic<uint64_t> transform_calls{0};
    std::atomic<uint64_t> transform_pixels{0};
    std::atomic<uint64_t> projected_winding_flips{0};
    std::atomic<uint64_t> effect_stack_calls{0};
    std::atomic<uint64_t> effect_pixels{0};
    std::atomic<uint64_t> layer_culling_tests{0};
    std::atomic<uint64_t> layers_culled{0};
    std::atomic<uint64_t> layers_visible{0};
    std::atomic<uint64_t> framebuffer_allocations{0};
    std::atomic<uint64_t> framebuffer_reuses{0};
    std::atomic<uint64_t> framebuffer_bytes_allocated{0};
    std::atomic<uint64_t> framebuffer_bytes_peak{0};

    std::atomic<uint64_t> dirty_rect_count{0};
    std::atomic<uint64_t> dirty_pixels{0};
    std::atomic<uint64_t> dirty_union_area_pixels{0};
    std::atomic<uint64_t> dirty_full_fallbacks{0};
    std::array<std::atomic<uint64_t>, dirty_fallback_reason_count()> dirty_full_fallback_reasons{};

    std::atomic<uint64_t> bypass_not_cacheable_count{0};

    RenderCounters() = default;

    // ── Internal helper: copy all scalar fields from another RenderCounters ───
    // Avoids duplicating the field list across move ctor, copy ctor, and both
    // assignment operators.  Add new fields *here only* and they propagate.
private:
    template <typename T>
    void copy_all_fields_from(T& other) {
        pixels_touched.store(other.pixels_touched.load(std::memory_order_relaxed), std::memory_order_relaxed);
        cache_hits.store(other.cache_hits.load(std::memory_order_relaxed), std::memory_order_relaxed);
        cache_misses.store(other.cache_misses.load(std::memory_order_relaxed), std::memory_order_relaxed);
        nodes_executed.store(other.nodes_executed.load(std::memory_order_relaxed), std::memory_order_relaxed);
        layers_rendered.store(other.layers_rendered.load(std::memory_order_relaxed), std::memory_order_relaxed);
        text_glyphs_rasterized.store(other.text_glyphs_rasterized.load(std::memory_order_relaxed), std::memory_order_relaxed);
        images_sampled.store(other.images_sampled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        blur_pixels.store(other.blur_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        simd_lerp_calls.store(other.simd_lerp_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tiles_total.store(other.tiles_total.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tiles_hit.store(other.tiles_hit.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tiles_miss.store(other.tiles_miss.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tiles_partial.store(other.tiles_partial.load(std::memory_order_relaxed), std::memory_order_relaxed);
        node_cache_hash_collisions.store(other.node_cache_hash_collisions.load(std::memory_order_relaxed), std::memory_order_relaxed);
        clear_calls.store(other.clear_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        clear_pixels.store(other.clear_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        clear_copy_pixels.store(other.clear_copy_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        composite_calls.store(other.composite_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        composite_pixels.store(other.composite_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        transform_calls.store(other.transform_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        transform_pixels.store(other.transform_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        projected_winding_flips.store(other.projected_winding_flips.load(std::memory_order_relaxed), std::memory_order_relaxed);
        effect_stack_calls.store(other.effect_stack_calls.load(std::memory_order_relaxed), std::memory_order_relaxed);
        effect_pixels.store(other.effect_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        layer_culling_tests.store(other.layer_culling_tests.load(std::memory_order_relaxed), std::memory_order_relaxed);
        layers_culled.store(other.layers_culled.load(std::memory_order_relaxed), std::memory_order_relaxed);
        layers_visible.store(other.layers_visible.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_allocations.store(other.framebuffer_allocations.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_reuses.store(other.framebuffer_reuses.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_bytes_allocated.store(other.framebuffer_bytes_allocated.load(std::memory_order_relaxed), std::memory_order_relaxed);
        framebuffer_bytes_peak.store(other.framebuffer_bytes_peak.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dirty_rect_count.store(other.dirty_rect_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dirty_pixels.store(other.dirty_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dirty_union_area_pixels.store(other.dirty_union_area_pixels.load(std::memory_order_relaxed), std::memory_order_relaxed);
        dirty_full_fallbacks.store(other.dirty_full_fallbacks.load(std::memory_order_relaxed), std::memory_order_relaxed);
        for (std::size_t i = 0; i < dirty_fallback_reason_count(); ++i) {
            dirty_full_fallback_reasons[i].store(
                other.dirty_full_fallback_reasons[i].load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
        }
    }

public:
    RenderCounters(RenderCounters&& other) noexcept {
        copy_all_fields_from(other);
    }

    RenderCounters& operator=(RenderCounters&& other) noexcept {
        if (this != &other) {
            copy_all_fields_from(other);
        }
        return *this;
    }

    RenderCounters(const RenderCounters& other) {
        copy_all_fields_from(other);
    }

    RenderCounters& operator=(const RenderCounters& other) {
        if (this != &other) {
            copy_all_fields_from(other);
        }
        return *this;
    }

    void reset() {
        pixels_touched.store(0, std::memory_order_relaxed);
        cache_hits.store(0, std::memory_order_relaxed);
        cache_misses.store(0, std::memory_order_relaxed);
        nodes_executed.store(0, std::memory_order_relaxed);
        layers_rendered.store(0, std::memory_order_relaxed);
        text_glyphs_rasterized.store(0, std::memory_order_relaxed);
        images_sampled.store(0, std::memory_order_relaxed);
        blur_pixels.store(0, std::memory_order_relaxed);
        simd_lerp_calls.store(0, std::memory_order_relaxed);
        tiles_total.store(0, std::memory_order_relaxed);
        tiles_hit.store(0, std::memory_order_relaxed);
        tiles_miss.store(0, std::memory_order_relaxed);
        tiles_partial.store(0, std::memory_order_relaxed);
        node_cache_hash_collisions.store(0, std::memory_order_relaxed);
        clear_calls.store(0, std::memory_order_relaxed);
        clear_pixels.store(0, std::memory_order_relaxed);
        clear_copy_pixels.store(0, std::memory_order_relaxed);
        composite_calls.store(0, std::memory_order_relaxed);
        composite_pixels.store(0, std::memory_order_relaxed);
        transform_calls.store(0, std::memory_order_relaxed);
        transform_pixels.store(0, std::memory_order_relaxed);
        projected_winding_flips.store(0, std::memory_order_relaxed);
        effect_stack_calls.store(0, std::memory_order_relaxed);
        effect_pixels.store(0, std::memory_order_relaxed);
        layer_culling_tests.store(0, std::memory_order_relaxed);
        layers_culled.store(0, std::memory_order_relaxed);
        layers_visible.store(0, std::memory_order_relaxed);
        framebuffer_allocations.store(0, std::memory_order_relaxed);
        framebuffer_reuses.store(0, std::memory_order_relaxed);
        framebuffer_bytes_allocated.store(0, std::memory_order_relaxed);
        framebuffer_bytes_peak.store(0, std::memory_order_relaxed);
        dirty_rect_count.store(0, std::memory_order_relaxed);
        dirty_pixels.store(0, std::memory_order_relaxed);
        dirty_union_area_pixels.store(0, std::memory_order_relaxed);
        dirty_full_fallbacks.store(0, std::memory_order_relaxed);
        bypass_not_cacheable_count.store(0, std::memory_order_relaxed);
        for (auto& reason : dirty_full_fallback_reasons) {
            reason.store(0, std::memory_order_relaxed);
        }
    }

    void increment_dirty_full_fallback_reason(DirtyFallbackReason reason) {
        const auto index = static_cast<std::size_t>(reason);
        if (index < dirty_fallback_reason_count()) {
            dirty_full_fallback_reasons[index].fetch_add(1, std::memory_order_relaxed);
        }
    }
};

} // namespace chronon3d
