#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    RenderProfiler* profiler{nullptr};
    const CompositionRegistry* registry{nullptr};
    video::VideoFrameDecoder* video_decoder{nullptr};

    chronon3d::RenderTrace* trace{nullptr};
    chronon3d::RenderCounters* counters{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};

    // ── Per-node / per-layer telemetry collectors ────────────────────────────
    // Populated during graph execution; flushed via TelemetryManager after frame.
    std::vector<chronon3d::telemetry::NodeTelemetryRecord> node_telemetry;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_telemetry;
};

class RenderGraphNode {
public:
    virtual ~RenderGraphNode() = default;

    [[nodiscard]] virtual RenderGraphNodeKind kind() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] std::string layer_id() const { return m_layer_id; }
    void set_layer_id(std::string id) { m_layer_id = std::move(id); }

    [[nodiscard]] virtual bool cacheable() const { return true; }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>& inputs
    ) = 0;

private:
    std::string m_layer_id;
};

} // namespace chronon3d::graph
