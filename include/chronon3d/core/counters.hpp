#pragma once
#include <atomic>
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

    RenderCounters() = default;

    RenderCounters(RenderCounters&& other) noexcept {
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
    }

    RenderCounters& operator=(RenderCounters&& other) noexcept {
        if (this != &other) {
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
        }
        return *this;
    }

    RenderCounters(const RenderCounters& other) {
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
    }

    RenderCounters& operator=(const RenderCounters& other) {
        if (this != &other) {
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
    }
};

} // namespace chronon3d
