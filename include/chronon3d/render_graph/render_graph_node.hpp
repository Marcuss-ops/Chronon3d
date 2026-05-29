#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/cache_policy.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <span>

namespace chronon3d {
    class CompositionRegistry;
    struct RenderCounters;
}

namespace chronon3d::video {
    class VideoFrameDecoder;
}

namespace chronon3d::graph {

class RenderBackend;
class RenderProfiler;
using GraphNodeId = uint32_t;

enum class CacheFramePolicy {
    FrameDependent,
    FrameInvariant
};

enum class RenderGraphNodeKind {
    Source,
    Mask,
    Effect,
    Transform,
    Composite,
    Precomp,
    Video,
    Adjustment,
    MotionBlur,
    ColorConvert,
    TrackMatte,
    Output,
    Transition
};

[[nodiscard]] inline std::string_view to_string(RenderGraphNodeKind kind) {
    return enum_utils::enum_name_exact(kind);
}

struct RenderGraphContext {
    Frame frame{0};
    float time_seconds{0.0f};
    float fps{30.0f};
    int width{0};
    int height{0};
    Camera camera{};

    // 2.5D camera — populated from scene.camera_2_5d() when active.
    // Processors use this to build ProjectionContext for direct card rendering.
    Camera2_5D camera_2_5d{};
    bool has_camera_2_5d{false};
    renderer::ProjectionContext projection_ctx{}; // pre-built from camera_2_5d
    rendering::LightContext light_context{};

    RenderBackend* backend{nullptr};
    cache::NodeCache* node_cache{nullptr};
    std::shared_ptr<cache::FramebufferPool> framebuffer_pool;
    std::optional<raster::BBox> dirty_rect;

    std::shared_ptr<Framebuffer> acquire_framebuffer(
        int w,
        int h,
        bool clear = true,
        std::optional<raster::BBox> bounds = std::nullopt,
        std::atomic<uint64_t>* specific_clear_ms = nullptr
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

    std::shared_ptr<Framebuffer> acquire_framebuffer(const Framebuffer& other) const {
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

    // ── Early-exit optimization: when a full-frame opaque layer is found
    //     during graph building, the nodes it covers are flagged here.
    //     The executor skips flagged nodes entirely.
    std::vector<bool> early_exit_skip;

    // ── Graph structure unchanged hint ──────────────────────────────────
    // When the scene static fingerprint matches the previous frame and the
    // camera hasn't changed, the resulting render graph is guaranteed to be
    // topologically identical.  The executor can skip compute_structure_
    // signature() and reuse the cached execution plan directly.
    bool graph_structure_unchanged{false};

    // ── Per-node / per-layer telemetry collectors ────────────────────────────
    // Populated during graph execution; flushed via TelemetryManager after frame.
    std::vector<chronon3d::telemetry::NodeTelemetryRecord> node_telemetry;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_telemetry;
};

class RenderGraphNode {
public:
    virtual ~RenderGraphNode() = default;

    [[nodiscard]]    virtual std::optional<raster::BBox> predicted_bbox(const RenderGraphContext& ctx) const {
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) const {
        (void)input_bboxes;
        return predicted_bbox(ctx);
    }
    
    virtual RenderGraphNodeKind kind() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] std::string layer_id() const { return m_layer_id; }
    void set_layer_id(std::string id) { m_layer_id = std::move(id); }

    [[nodiscard]] virtual bool cacheable() const { return true; }

    /// Returns true when the node can serve as a fully opaque full-frame seed
    /// for the first layer in a composition. This lets the builder skip the
    /// initial clear/composite pass for static full-frame backgrounds.
    [[nodiscard]] virtual bool can_seed_full_frame(const RenderGraphContext&) const {
        return false;
    }

    [[nodiscard]] virtual CacheFramePolicy cache_frame_policy() const {
        return CacheFramePolicy::FrameDependent;
    }

    /// Rich cache policy descriptor.  Default implementation wraps the legacy
    /// cacheable() / cache_frame_policy() / frame_dependent() API.
    [[nodiscard]] virtual RenderNodeCachePolicy cache_policy() const {
        const bool invariant = cache_frame_policy() == CacheFramePolicy::FrameInvariant;
        return RenderNodeCachePolicy{
            .cacheable = cacheable(),
            .frame_dependent = frame_dependent() || cache_frame_policy() == CacheFramePolicy::FrameDependent,
            .frame_invariant = invariant,
            .disk_cacheable = invariant,
            .lifetime = invariant
                ? CacheLifetime::PersistentDisk
                : CacheLifetime::PerFrame,
            .invalidation = invariant
                ? CacheInvalidation::WhenParamsChange
                : CacheInvalidation::WhenInputsChange,
            .debug_reason = "legacy_policy"
        };
    }

    [[nodiscard]] bool frame_dependent() const { return m_frame_dependent; }
    void set_frame_dependent(bool value) { m_frame_dependent = value; }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) = 0;

private:
    std::string m_layer_id;
    bool m_frame_dependent{true};
};

} // namespace chronon3d::graph
