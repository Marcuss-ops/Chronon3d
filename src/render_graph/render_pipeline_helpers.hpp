#pragma once

// ---------------------------------------------------------------------------
// render_pipeline_helpers.hpp
//
// Internal helpers extracted from render_pipeline.cpp to keep that file
// focused on pipeline orchestration logic only.
//
// All functions are in namespace chronon3d::graph — same as render_pipeline.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <algorithm>
#include <cmath>
#include <functional>

namespace chronon3d::graph {

// ── Scene fingerprinting ─────────────────────────────────────────────────────
//
// Hashes the visible scene state at a given frame into a 64-bit digest.
// Used by the dirty-rect fast path to skip full re-renders when nothing changed.
[[nodiscard]] inline uint64_t compute_scene_fingerprint(const Scene& scene, Frame frame) {
    uint64_t h = 0;
    
    // We only include frame if there are animated properties that aren't 
    // captured by the layer transforms or node params.
    // Most animations in Chronon are captured via these fields.
    // combine(h, std::hash<int64_t>{}(static_cast<int64_t>(frame)));

    for (const auto& layer : scene.layers()) {
        if (!layer.active_at(frame)) continue;

        h = hash_combine(h, layer.get_static_hash());
        h = hash_combine(h, hash_transform(layer.transform));
    }
    return h;
}

// ── Telemetry row builder ────────────────────────────────────────────────────
[[nodiscard]] inline telemetry::RenderTelemetryRow make_telemetry_row(
    std::string event,
    Frame frame,
    int width,
    int height,
    double total_ms,
    double setup_ms,
    double composite_ms,
    double blur_ms,
    double encode_ms,
    int cache_hit,
    int layer_count,
    const RenderCounters* counters
) {
    telemetry::RenderTelemetryRow row;
    row.event = event;
    row.frame = frame;
    row.width = width;
    row.height = height;
    row.total_ms = total_ms;
    row.setup_ms = setup_ms;
    row.composite_ms = composite_ms;
    row.blur_ms = blur_ms;
    row.encode_ms = encode_ms;
    row.cache_hit = cache_hit;
    row.layer_count = layer_count;
    if (counters) {
        row.cache_hits = counters->cache_hits.load(std::memory_order_relaxed);
        row.cache_misses = counters->cache_misses.load(std::memory_order_relaxed);
        row.nodes_executed = counters->nodes_executed.load(std::memory_order_relaxed);
        row.clear_calls = counters->clear_calls.load(std::memory_order_relaxed);
        row.clear_pixels = counters->clear_pixels.load(std::memory_order_relaxed);
        row.composite_calls = counters->composite_calls.load(std::memory_order_relaxed);
        row.composite_pixels = counters->composite_pixels.load(std::memory_order_relaxed);
        row.transform_calls = counters->transform_calls.load(std::memory_order_relaxed);
        row.transform_pixels = counters->transform_pixels.load(std::memory_order_relaxed);
        row.effect_stack_calls = counters->effect_stack_calls.load(std::memory_order_relaxed);
        row.effect_pixels = counters->effect_pixels.load(std::memory_order_relaxed);
        row.text_glyphs_rasterized = counters->text_glyphs_rasterized.load(std::memory_order_relaxed);
        row.framebuffer_allocations = counters->framebuffer_allocations.load(std::memory_order_relaxed);
        row.framebuffer_reuses = counters->framebuffer_reuses.load(std::memory_order_relaxed);
        row.dirty_full_fallbacks = counters->dirty_full_fallbacks.load(std::memory_order_relaxed);
        row.dirty_full_fallback_predicted_bounds_missing =
            counters->dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::PredictedBoundsMissing)]
                .load(std::memory_order_relaxed);
        row.dirty_full_fallback_composite_missing_input_bounds =
            counters->dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::CompositeMissingInputBounds)]
                .load(std::memory_order_relaxed);
        row.dirty_full_fallback_transform_bounds_unknown =
            counters->dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::TransformBoundsUnknown)]
                .load(std::memory_order_relaxed);
        row.dirty_full_fallback_effect_bounds_unknown =
            counters->dirty_full_fallback_reasons[static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)]
                .load(std::memory_order_relaxed);
        row.framebuffer_acquire_ms = counters->framebuffer_acquire_ms.load(std::memory_order_relaxed);
        row.framebuffer_clear_ms = counters->framebuffer_clear_ms.load(std::memory_order_relaxed);
        row.clearnode_ms = counters->clearnode_ms.load(std::memory_order_relaxed);
        row.framebuffer_pool_clear_ms = counters->framebuffer_pool_clear_ms.load(std::memory_order_relaxed);
        row.framebuffer_enqueue_ms = counters->framebuffer_enqueue_ms.load(std::memory_order_relaxed);
        row.framebuffer_pool_miss_count_size_mismatch = counters->framebuffer_pool_miss_count_size_mismatch.load(std::memory_order_relaxed);
        row.framebuffer_pool_miss_count_empty = counters->framebuffer_pool_miss_count_empty.load(std::memory_order_relaxed);
        row.framebuffer_pool_hits = counters->framebuffer_pool_hits.load(std::memory_order_relaxed);
        row.framebuffer_buffer_returned_to_pool_count = counters->framebuffer_buffer_returned_to_pool_count.load(std::memory_order_relaxed);
        row.unaligned_memory_copies = counters->unaligned_memory_copies.load(std::memory_order_relaxed);
        row.frame_conversion_copy_ms = counters->frame_conversion_copy_ms.load(std::memory_order_relaxed);
        row.video_graph_eval_ms = counters->video_graph_eval_ms.load(std::memory_order_relaxed);
        row.video_conversion_ms = counters->video_conversion_ms.load(std::memory_order_relaxed);
        row.video_pipe_write_ms = counters->video_pipe_write_ms.load(std::memory_order_relaxed);
        row.video_ffmpeg_latency_ms = counters->video_ffmpeg_latency_ms.load(std::memory_order_relaxed);
        row.io_queue_push_blocked_ms = counters->io_queue_push_blocked_ms.load(std::memory_order_relaxed);
        row.io_queue_pop_wait_ms = counters->io_queue_pop_wait_ms.load(std::memory_order_relaxed);
        row.io_queue_peak_depth = counters->io_queue_peak_depth.load(std::memory_order_relaxed);
        row.ffmpeg_pipe_write_blocked_ms = counters->ffmpeg_pipe_write_blocked_ms.load(std::memory_order_relaxed);
        row.converted_frame_cache_hits = counters->converted_frame_cache_hits.load(std::memory_order_relaxed);
        row.ffmpeg_flush_ms = counters->ffmpeg_flush_ms.load(std::memory_order_relaxed);
    }
    return row;
}

// ── Framebuffer downsampling ─────────────────────────────────────────────────
//
// Box-filter downsample used by the SSAA path in render_composition_frame().
[[nodiscard]] inline std::unique_ptr<Framebuffer> downsample_fb(
    const Framebuffer& src, i32 dst_w, i32 dst_h
) {
    auto dst = std::make_unique<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width())  / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            const int x0 = static_cast<int>(x * sx);
            const int y0 = static_cast<int>(y * sy);
            const int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            const int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());
            for (int iy = y0; iy < y1; ++iy) {
                for (int ix = x0; ix < x1; ++ix) {
                    const Color c = src.get_pixel(ix, iy);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    ++count;
                }
            }
            if (count > 0) {
                const float inv = 1.0f / count;
                dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
            }
        }
    }
    return dst;
}

// ── RenderGraphContext factory ───────────────────────────────────────────────
[[nodiscard]] inline RenderGraphContext make_graph_context(
    RenderBackend& backend,
    cache::NodeCache& node_cache,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time,
    const RenderSettings& settings,
    const CompositionRegistry* registry,
    video::VideoFrameDecoder* video_decoder,
    float fps
) {
    return RenderGraphContext{
        .frame = frame,
        .time_seconds = static_cast<float>(frame) + frame_time,
        .fps = fps,
        .width = width,
        .height = height,
        .camera = camera,
        .backend = &backend,
        .node_cache = &node_cache,
        .framebuffer_pool = backend.framebuffer_pool(),
        .registry = registry,
        .video_decoder = video_decoder,
        .trace = backend.trace(),
        .counters = backend.counters(),
        .diagnostics_enabled = settings.diagnostic,
        .ssaa_factor = settings.ssaa_factor,
        .modular_coordinates = settings.use_modular_graph,
        .tile_size = settings.tile_size,
        .optimize_compositing = settings.optimize_compositing,
        // Keep node clipping in sync with framebuffer reuse.
        // Without this, the Clear node can erase an entire reused buffer,
        // which wipes cached full-frame backgrounds on the next frame.
        .dirty_rects_enabled = settings.enable_dirty_rects || settings.dirty_rects || settings.enable_dirty_bitmask
    };
}

} // namespace chronon3d::graph
