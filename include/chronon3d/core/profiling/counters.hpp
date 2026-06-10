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
    X(glow_cache_hits) \
    X(glow_cache_misses) \
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
    X(clearnode_copy_pixels) \
    X(composite_copy_pixels) \
    X(clearnode_bytes_avoided) \
    X(clearnode_memcpy_bytes) \
    X(clearnode_memcpy_calls) \
    X(clearnode_detach_shared_count) \
    X(clearnode_partial_clip_copy_count) \
    X(clearnode_full_clip_skip_count) \
    X(prev_fb_use_count_sum) \
    X(prev_fb_use_count_samples) \
    X(prev_fb_use_count_peak) \
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
    X(graph_cache_hits) \
    X(graph_cache_misses) \
    X(execution_plan_cache_hits) \
    X(graph_resolve_layers_ms) \
    X(graph_dirty_rect_ms) \
    X(graph_build_ms) \
    X(graph_execute_ms) \
    X(graph_total_ms) \
    X(compiled_graph_refresh_ms) \
    X(cache_eval_ms) \
    X(dirty_eval_ms) \
    X(input_resolve_ms) \
    X(framebuffer_lifetime_ms) \
    X(node_schedule_ms) \
    X(node_dispatch_ms) \
    X(node_execute_actual_ms) \
    X(node_overhead_ms) \
    X(level_parallel_count) \
    X(level_sequential_count) \
    X(telemetry_emit_ms) \
    X(predicted_bbox_ms) \
    X(clone_context_ms) \
    X(state_assign_ms) \
    X(bypass_not_cacheable_count) \
    X(nodes_skipped) \
    X(framebuffer_acquire_ms) \
    X(framebuffer_clear_ms) \
    X(clearnode_ms) \
    X(clearnode_memcpy_ms) \
    X(clearnode_restore_ms) \
    X(clearnode_acquire_ms) \
    X(clearnode_clear_ms) \
    X(compositenode_blend_ms) \
    X(compositenode_setup_ms) \
    X(compositenode_copy_ms) \
    X(compositenode_row_ms) \
    X(framebuffer_pool_clear_ms) \
    X(framebuffer_enqueue_ms) \
    X(framebuffer_pool_miss_count_size_mismatch) \
    X(framebuffer_pool_miss_count_empty) \
    X(framebuffer_pool_miss_count_best_fit) \
    X(framebuffer_pool_hits) \
    X(framebuffer_buffer_returned_to_pool_count) \
    X(framebuffer_prealloc_created) \
    X(framebuffer_copy_ms) \
    X(framebuffer_copy_parallel_calls) \
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
    X(native_av_convert_skipped_ms) \
    X(converted_frame_cache_hits) \
    X(converted_frame_cache_misses) \
    X(ffmpeg_flush_ms) \
    X(io_queue_peak_bytes) \
    X(graph_executed_frames) \
    X(graph_skipped_frames) \
    X(graph_executed_ms_sum) \
    X(graph_skipped_ms_sum)

#define CHRONON_RENDER_COUNTERS_SYSTEM(X) \
    X(process_context_switches_voluntary) \
    X(process_context_switches_involuntary) \
    X(os_page_faults_major) \
    X(os_page_faults_minor) \
    X(ffmpeg_cpu_user_pct) \
    X(ffmpeg_cpu_sys_pct) \
    X(llc_references) \
    X(llc_misses) \
    X(system_logical_cores) \
    X(system_ram_total_mb) \
    X(system_ram_available_min_mb) \
    X(process_cpu_user_ms) \
    X(process_cpu_sys_ms) \
    X(process_rss_peak_mb) \
    X(tbb_arena_max_concurrency) \
    X(tbb_active_workers_peak) \
    X(tbb_active_workers_avg_sum) \
    X(tbb_active_workers_avg_count) \
    X(parallel_regions_count) \
    X(parallel_regions_skipped_small_level) \
    X(parallel_idle_worker_entry_sum) \
    X(parallel_idle_worker_samples) \
    X(sequential_level_execute_ms) \
    X(used_parallel_clear) \
    X(skipped_clear_small) \
    X(used_parallel_transform) \
    X(skipped_transform_small) \
    X(used_parallel_composite) \
    X(skipped_composite_small) \
    X(skipped_encoder_backpressure)

#define CHRONON_RENDER_COUNTERS_SETUP(X) \
    X(setup_graph_parsing_ms) \
    X(setup_asset_io_load_ms) \
    X(setup_pool_preallocation_ms) \
    X(image_decode_ms)

// ---------------------------------------------------------------------------
// Raw (non-atomic) counters for thread-local accumulation.
//
// Each thread keeps its own RenderCountersRaw and increments the fields with
// plain `uint64_t` ops (no atomic RMW).  At the end of a frame, the per-thread
// raw is flushed into the global atomic RenderCounters via merge_tls().
//
// This is the key anti-false-sharing strategy: N threads doing 155 plain
// integer adds each is roughly N x 155 x ~1 ns = sub-microsecond.  The old
// approach (155 atomic fetch_adds, with cache-line bouncing between cores on
// every write) was the dominant cost in tight parallel regions.
//
// The fields are NOT individually aligned here because we never share them
// across threads: each thread has its own RenderCountersRaw.
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

// ---------------------------------------------------------------------------
// RenderCounters — the global, process-wide counter store.
//
// Every field is alignas(64) (one cache line) so that concurrent writers
// from different cores do not share a cache line.  This eliminates the
// "false sharing" pathology where 4 cores updating 4 different 8-byte
// atomics all ping-pong the same 64-byte line through L1 / L2 / L3.
//
// The fields are grouped (RENDER / SYSTEM / SETUP) so that a hot field
// (e.g. `pixels_touched`) does not share a cache line with a cold field
// (e.g. `system_ram_total_mb`).  The grouping is purely for locality —
// each individual field still gets its own 64-byte slot.
//
// For high-frequency counters in tight parallel loops, prefer the
// thread-local pattern: per-thread plain `++` on `thread_local_counters()`,
// then a single `merge_tls()` per thread at the end of the frame.
// ---------------------------------------------------------------------------
struct RenderCounters {
    // Hot counters — written by render hot loops on every frame.
#define X(name) alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS(X)
#undef X

    // The dirty-fallback reason array: each slot gets its own cache line so
    // bumping one reason does not invalidate the cache line of another.
    alignas(kHardwareDestructiveInterferenceSize)
    std::array<std::atomic<uint64_t>, dirty_fallback_reason_count()>
        dirty_full_fallback_reasons{};

    // System / setup counters — written infrequently (sampler tick or
    // once per frame).  They are also given 64-byte slots so that the
    // "thundering herd" of render threads writing hot counters does not
    // bounce these cold lines through L1.
#define X(name) alignas(kHardwareDestructiveInterferenceSize) std::atomic<uint64_t> name{0};
    CHRONON_RENDER_COUNTERS_SYSTEM(X)
    CHRONON_RENDER_COUNTERS_SETUP(X)
#undef X

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
    ///
    /// This function is the "anti-false-sharing" producer: it is the only
    /// place where thread-local writes cross into the shared atomic domain.
    /// The number of cross-domain transactions is O(num_threads) per frame,
    /// not O(num_threads * num_counters).
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

// ---------------------------------------------------------------------------
// Thread-local counter accessor.
//
// Each thread sees its own RenderCountersRaw instance, lazily initialised
// on first use.  Workers should increment fields on this local with plain
// `++` (no atomic op, no cache-line bouncing), then call merge_tls() once
// per frame on the global RenderCounters.
//
// Usage:
//   // In the render hot path:
//   auto& c = thread_local_counters();
//   c.pixels_touched += 1;
//   c.composite_pixels += w * h;
//
//   // At end of frame (once per thread, on the worker itself):
//   global_counters.merge_tls(c);
//   c.reset();   // optional — if you want to keep the same thread-local
//                // across frames, reset after the merge.  Otherwise the
//                // counters will accumulate across frames and the merge
//                // will keep growing — fine for a single render, not for
//                // continuous runs.
//
// This accessor is intentionally in this header (not a separate .cpp) so
// the compiler can fully inline `thread_local_counters()` at every call
// site: a single TLS load + a single pointer return.
// ---------------------------------------------------------------------------
inline RenderCountersRaw& thread_local_counters() {
    thread_local RenderCountersRaw tls;
    return tls;
}

/// Number of fields in RenderCounters / RenderCountersRaw.
/// Used by tests / benchmarks to compute throughput.
///
/// Implementation: RenderCountersRaw is a POD struct whose only members are
/// `uint64_t` (X-macro generated) plus the `dirty_full_fallback_reasons`
/// array of `uint64_t`.  `sizeof(RenderCountersRaw) / sizeof(uint64_t)`
/// therefore gives the exact total number of uint64_t fields including the
/// array slots, which is what the tests / benchmarks want.
constexpr std::size_t render_counters_field_count() {
    return sizeof(RenderCountersRaw) / sizeof(uint64_t);
}

} // namespace chronon3d
