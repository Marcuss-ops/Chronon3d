#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>
#include <atomic>

namespace chronon3d {
    class CompositionRegistry;
    struct RenderCounters;
}

namespace chronon3d::video {
    class VideoFrameDecoder;
}

namespace chronon3d::cache {
    class NodeCache;
    class FramebufferPool;
}

namespace chronon3d::graph {

class RenderBackend;
class RenderProfiler;

struct RenderGraphContext {
    Frame frame{0};
    float time_seconds{0.0f};
    float fps{30.0f};
    int width{0};
    int height{0};
    Camera camera{};

    // 2.5D camera — populated from scene.camera_2_5d() when active.
    Camera2_5D camera_2_5d{};
    bool has_camera_2_5d{false};
    renderer::ProjectionContext projection_ctx{}; // pre-built from camera_2_5d
    rendering::LightContext light_context{};

    RenderBackend* backend{nullptr};
    cache::NodeCache* node_cache{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
    std::optional<raster::BBox> dirty_rect;

    /// Acquire a framebuffer as an OwnedFB (unique_ptr with pool deleter).
    OwnedFB acquire_owned_fb(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt,
        std::atomic<uint64_t>* specific_clear_ms = nullptr
    ) const;

    /// Copy an existing framebuffer into a new OwnedFB.
    OwnedFB acquire_owned_fb(const Framebuffer& other) const;

    /// Adopt a uniquely-owned shared_ptr<Framebuffer> into an OwnedFB without copying pixel data.
    OwnedFB acquire_owned_fb(std::shared_ptr<Framebuffer>&& src) const;

    /// Convert an OwnedFB to a shared_ptr for cache storage.
    static CachedFB own_to_cache(OwnedFB& owned, cache::FramebufferPool* pool);

    /// Legacy shared_ptr acquire — still needed for cache interaction.
    std::shared_ptr<Framebuffer> acquire_framebuffer(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt,
        std::atomic<uint64_t>* specific_clear_ms = nullptr
    ) const;

    std::shared_ptr<Framebuffer> acquire_framebuffer(const Framebuffer& other) const;

    RenderProfiler* profiler{nullptr};
    const CompositionRegistry* registry{nullptr};
    video::VideoFrameDecoder* video_decoder{nullptr};

    chronon3d::RenderCounters* counters{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};

    // Tiling and clipping
    int tile_size{0};
    std::optional<raster::BBox> clip_rect;

    // Tile-based execution mode (Branch 3+): when true, the executor should
    // include tile coordinates in cache keys to prevent cross-tile staleness.
    bool tile_execution_enabled{false};
    std::optional<raster::BBox> active_tile_clip;

    // Optimization flags
    bool optimize_compositing{true};
    bool dirty_rects_enabled{false};
    bool reuse_prev_framebuffer{false};
    bool skip_initial_clear{false};

    // ── Early-exit optimization
    std::vector<bool> early_exit_skip;

    // ── Graph structure unchanged hint
    bool graph_structure_unchanged{false};

    // ── Per-node / per-layer telemetry collectors
    std::vector<chronon3d::telemetry::NodeTelemetryRecord> node_telemetry;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_telemetry;

    // ── Per-pixel DOF depth tracking
    bool                      track_dof_depth{false};
    std::vector<float>        dof_depth;

    // Track reusable inputs to allow in-place swap optimization during node execution
    mutable std::vector<Framebuffer*> reusable_inputs;
};

} // namespace chronon3d::graph
