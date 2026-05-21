#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace chronon3d {
    class CompositionRegistry;
    class RenderTrace;
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
    Output
};

[[nodiscard]] inline std::string_view to_string(RenderGraphNodeKind kind) {
    using enum RenderGraphNodeKind;
    switch (kind) {
        case Source:         return "Source";
        case Mask:           return "Mask";
        case Effect:         return "Effect";
        case Transform:      return "Transform";
        case Composite:      return "Composite";
        case Precomp:        return "Precomp";
        case Video:          return "Video";
        case Adjustment:     return "Adjustment";
        case MotionBlur:     return "MotionBlur";
        case ColorConvert:   return "ColorConvert";
        case TrackMatte:     return "TrackMatte";
        case Output:         return "Output";
    }
    return "Unknown";
}

struct RenderGraphContext {
    Frame frame{0};
    float time_seconds{0.0f};
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

    std::shared_ptr<Framebuffer> acquire_framebuffer(int w, int h, bool clear = true) const {
        if (framebuffer_pool) {
            return framebuffer_pool->acquire_pooled(w, h, framebuffer_pool, clear);
        }
        auto fb = std::make_shared<Framebuffer>(w, h);
        if (clear) {
            fb->clear(Color::transparent());
        }
        return fb;
    }

    std::shared_ptr<Framebuffer> acquire_framebuffer(const Framebuffer& other) const {
        auto fb = acquire_framebuffer(other.width(), other.height(), false);
        *fb = other;
        return fb;
    }

    RenderProfiler* profiler{nullptr};
    const CompositionRegistry* registry{nullptr};
    video::VideoFrameDecoder* video_decoder{nullptr};

    chronon3d::RenderTrace* trace{nullptr};
    chronon3d::RenderCounters* counters{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};

    // Tiling and clipping
    int tile_size{0};
    std::optional<raster::BBox> clip_rect;

    // Optimization flags
    bool optimize_compositing{true};

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
        const std::vector<std::optional<raster::BBox>>& input_bboxes
    ) const {
        (void)input_bboxes;
        return predicted_bbox(ctx);
    }
    
    virtual RenderGraphNodeKind kind() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] std::string layer_id() const { return m_layer_id; }
    void set_layer_id(std::string id) { m_layer_id = std::move(id); }

    [[nodiscard]] virtual bool cacheable() const { return true; }

    [[nodiscard]] virtual CacheFramePolicy cache_frame_policy() const {
        return CacheFramePolicy::FrameDependent;
    }

    [[nodiscard]] bool frame_dependent() const { return m_frame_dependent; }
    void set_frame_dependent(bool value) { m_frame_dependent = value; }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>& inputs,
        const std::vector<std::optional<raster::BBox>>& input_bboxes
    ) = 0;

private:
    std::string m_layer_id;
    bool m_frame_dependent{true};
};

} // namespace chronon3d::graph
