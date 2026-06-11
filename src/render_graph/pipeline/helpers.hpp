#pragma once

// ---------------------------------------------------------------------------
// render_pipeline_helpers.hpp
//
// Internal helpers extracted from render_pipeline.cpp to keep that file
// focused on pipeline orchestration logic only.
//
// All functions are in namespace chronon3d::graph — same as render_pipeline.cpp.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/memory_utils.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <xxhash.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::graph {

// ── Scene fingerprinting ─────────────────────────────────────────────────────
//
// Hashes the visible scene state at a given frame into a 64-bit digest.
// Used by the dirty-rect fast path to skip full re-renders when nothing changed.
[[nodiscard]] inline uint64_t compute_scene_fingerprint(const Scene& scene, Frame frame) {
    XXH3_state_t state;
    XXH3_64bits_reset(&state);

    for (const auto& layer : scene.layers()) {
        if (!layer.active_at(frame)) continue;

        const u64 static_hash = layer.get_static_hash();
        XXH3_64bits_update(&state, &static_hash, sizeof(static_hash));
        
        const u64 transform_hash = hash_transform(layer.transform);
        XXH3_64bits_update(&state, &transform_hash, sizeof(transform_hash));
    }
    for (const auto& node : scene.nodes()) {
        const u64 node_hash = hash_render_node(node);
        XXH3_64bits_update(&state, &node_hash, sizeof(node_hash));
    }
    return XXH3_64bits_digest(&state);
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
    const int src_stride = src.allocated_width();
    const Color* src_data = src.data();
    const int dst_stride = dst->allocated_width();
    Color* dst_data = dst->data();
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            const int x0 = static_cast<int>(x * sx);
            const int y0 = static_cast<int>(y * sy);
            const int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            const int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());
            for (int iy = y0; iy < y1; ++iy) {
                // Prefetch next source row every 4 rows
                if ((iy & 3) == 0 && iy + 4 < y1) {
                    chronon3d::prefetch(src_data + static_cast<size_t>(iy + 4) * src_stride + x0, false, 1);
                }
                const Color* src_row = src_data + static_cast<size_t>(iy) * src_stride;
                for (int ix = x0; ix < x1; ++ix) {
                    const Color& c = src_row[ix];
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    ++count;
                }
            }
            if (count > 0) {
                const float inv = 1.0f / count;
                dst_data[static_cast<size_t>(y) * dst_stride + x] = Color{r * inv, g * inv, b * inv, a * inv};
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
    media::MediaFrameProvider* video_decoder,
    float fps
) {
    return RenderGraphContext{
        .frame = RenderFrameInfo{
            .frame = frame,
            .time_seconds = static_cast<float>(frame) + frame_time,
            .fps = fps,
            .width = width,
            .height = height,
        },
        .camera = RenderCameraContext{
            .camera = camera,
        },
        .resources = RenderResourceContext{
            .backend = &backend,
            .node_cache = &node_cache,
            .framebuffer_pool = backend.framebuffer_pool(),
            .registry = registry,
            .video_decoder = video_decoder,
        },
        .options = RenderOptimizationContext{
            .diagnostics_enabled = settings.diagnostics.enabled,
            .ssaa_factor = settings.ssaa_factor,
            .modular_coordinates = settings.use_modular_graph,
            .optimize_compositing = settings.compositing.optimize_compositing,
            // Keep node clipping in sync with framebuffer reuse.
            // Without this, the Clear node can erase an entire reused buffer,
            // which wipes cached full-frame backgrounds on the next frame.
            .dirty_rects_enabled = settings.dirty.is_active(),
        },
        .telemetry = RenderTelemetryContext{
            .counters = backend.counters(),
        },
        .tile = RenderTileContext{
            .tile_size = settings.dirty.tile_size,
        },
    };
}

[[nodiscard]] inline ResolvedCamera resolve_scene_camera(const Scene& scene) {
    return chronon3d::resolve_camera_hierarchy(scene.layers(), scene.resource(), scene.camera_2_5d());
}

} // namespace chronon3d::graph
