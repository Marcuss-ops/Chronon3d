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
    for (const auto& node : scene.nodes()) {
        h = hash_combine(h, hash_render_node(node));
    }
    return h;
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
