#pragma once
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <atomic>
#include <array>
#include <cstdint>
namespace chronon3d {

constexpr std::size_t kHardwareDestructiveInterferenceSize = 64;

#define CHRONON_RENDER_COUNTERS(X) \
    X(pixels_touched) \
    X(cache_hits) \
    X(cache_misses) \
    X(nodes_executed) \
    X(layers_rendered) \
    X(text_glyphs_rasterized) \
    X(text_cache_hits) \
    X(text_cache_misses) \
    X(text_layout_ms) \
    X(text_rasterization_ms) \
    X(text_shadow_cache_hits) \
    X(text_shadow_cache_misses) \
    X(text_glow_cache_hits) \
    X(text_glow_cache_misses) \
    X(images_sampled) \
    X(blur_pixels) \
    X(simd_lerp_calls) \
    X(tiles_total) \
    X(tiles_hit) \
    X(tiles_miss) \
    X(tiles_partial) \
    X(tile_dirty_count) \
    X(tile_clean_count) \
    X(tile_pixels_rendered) \
    X(tile_pixels_skipped) \
    X(tile_full_fallbacks) \
    X(node_cache_hash_collisions) \
    X(clear_skipped_calls) \
    X(clear_skipped_pixels) \
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
    X(execution_plan_cache_hits) \
    X(bypass_not_cacheable_count) \
    X(framebuffer_acquire_ms) \
    X(framebuffer_clear_ms) \
    X(clearnode_ms) \
    X(framebuffer_pool_clear_ms) \
    X(framebuffer_enqueue_ms) \
    X(framebuffer_pool_miss_count_size_mismatch) \
    X(framebuffer_pool_miss_count_empty) \
    X(framebuffer_pool_miss_count_best_fit) \
    X(framebuffer_pool_hits) \
    X(framebuffer_buffer_returned_to_pool_count) \
    X(unaligned_memory_copies) \
    X(frame_conversion_copy_ms) \
    X(video_graph_eval_ms) \
    X(video_conversion_ms) \
    X(video_pipe_write_ms) \
    X(video_ffmpeg_latency_ms) \
    X(io_queue_push_blocked_ms) \
    X(io_queue_pop_wait_ms) \
    X(io_writer_idle_wait_ms) \
    X(io_queue_peak_depth) \
    X(ffmpeg_pipe_write_blocked_ms) \
    X(native_av_convert_ms) \
    X(native_av_send_frame_ms) \
    X(native_av_receive_packet_ms) \
    X(native_av_mux_write_ms) \
    X(native_av_trailer_ms) \
    X(native_av_converted_frame_cache_hits) \
    X(native_av_converted_frame_cache_misses) \
    X(native_av_convert_skipped_ms) \
    X(converted_frame_cache_hits) \
    X(ffmpeg_flush_ms) \
    X(io_queue_peak_bytes)

#define CHRONON_RENDER_COUNTERS_SYSTEM(X) \
    X(process_context_switches_voluntary) \
    X(process_context_switches_involuntary) \
    X(os_page_faults_major) \
    X(os_page_faults_minor) \
    X(ffmpeg_cpu_user_pct) \
    X(ffmpeg_cpu_sys_pct) \
    X(llc_references) \
    X(llc_misses)

#define CHRONON_RENDER_COUNTERS_SETUP(X) \
    X(setup_graph_parsing_ms) \
    X(setup_asset_io_load_ms) \
    X(setup_pool_preallocation_ms) \
    X(image_decode_ms)

// ---------------------------------------------------------------------------
// Raw (non-atomic) counters for thread-local accumulation
// Same fields as RenderCounters but plain uint64_t — no atomic overhead.
// Use for per-thread accumulation; merge into RenderCounters at frame end.
// ---------------------------------------------------------------------------
struct RenderCountersRaw {
#define X(name) uint64_t name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X
    std::array<uint64_t, dirty_fallback_reason_count()> dirty_full_fallback_reasons{};
#define X(name) uint64_t name{0};
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X

    void reset() {
#define X(name) name = 0;
        CHRONON_RENDER_COUNTERS(X)
#undef X
        dirty_full_fallback_reasons.fill(0);
#define X(name) name = 0;
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
    }
};

struct RenderCounters {
#define X(name) alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X
#define X(name) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
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
#define X(name) name.store(other.name.load(std::memory_order_relaxed), std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
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
#define X(name) name.store(0, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
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

    /// Merge thread-local (non-atomic) raw counters into this atomic instance.
    /// Use at the end of each frame: threads accumulate into thread-local
    /// RenderCountersRaw, then call this once to flush into the global atomics.
    /// This replaces N atomic fetch_add calls per counter with a single pass.
    void merge_tls(const RenderCountersRaw& tls) {
#define X(name) name.fetch_add(tls.name, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS(X)
#undef X
        for (std::size_t i = 0; i < dirty_fallback_reason_count(); ++i) {
            dirty_full_fallback_reasons[i].fetch_add(
                tls.dirty_full_fallback_reasons[i], std::memory_order_relaxed);
        }
#define X(name) name.fetch_add(tls.name, std::memory_order_relaxed);
        CHRONON_RENDER_COUNTERS_SYSTEM(X)
        CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X
    }
};

} // namespace chronon3d
