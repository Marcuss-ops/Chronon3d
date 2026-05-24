#pragma once
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <atomic>
#include <array>
#include <cstdint>

namespace chronon3d {

#define CHRONON_RENDER_COUNTERS(X) \
    X(pixels_touched) \
    X(cache_hits) \
    X(cache_misses) \
    X(nodes_executed) \
    X(layers_rendered) \
    X(text_glyphs_rasterized) \
    X(images_sampled) \
    X(blur_pixels) \
    X(simd_lerp_calls) \
    X(tiles_total) \
    X(tiles_hit) \
    X(tiles_miss) \
    X(tiles_partial) \
    X(node_cache_hash_collisions) \
    X(clear_calls) \
    X(clear_pixels) \
    X(clear_copy_pixels) \
    X(composite_calls) \
    X(composite_pixels) \
    X(transform_calls) \
    X(transform_pixels) \
    X(projected_winding_flips) \
    X(effect_stack_calls) \
    X(effect_pixels) \
    X(layer_culling_tests) \
    X(layers_culled) \
    X(layers_visible) \
    X(framebuffer_allocations) \
    X(framebuffer_reuses) \
    X(framebuffer_bytes_allocated) \
    X(framebuffer_bytes_peak) \
    X(dirty_rect_count) \
    X(dirty_pixels) \
    X(dirty_union_area_pixels) \
    X(dirty_full_fallbacks) \
    X(bypass_not_cacheable_count) \
    X(framebuffer_acquire_ms) \
    X(framebuffer_clear_ms) \
    X(framebuffer_enqueue_ms) \
    X(framebuffer_pool_miss_count_size_mismatch) \
    X(framebuffer_pool_miss_count_empty) \
    X(framebuffer_buffer_returned_to_pool_count) \
    X(frame_conversion_copy_ms)

struct RenderCounters {
#define X(name) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X

    std::array<std::atomic<uint64_t>, dirty_fallback_reason_count()> dirty_full_fallback_reasons{};

    RenderCounters() = default;

private:
    template <typename T>
    void copy_all_fields_from(T& other) {
#define X(name) name.store(other.name.load(std::memory_order_relaxed), std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
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
#define X(name) name.store(0, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
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
