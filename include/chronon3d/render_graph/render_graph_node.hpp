#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/math/projection_context.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
    class SoftwareRenderer;
    class CompositionRegistry;
}

namespace chronon3d::video {
    class VideoFrameDecoder;
}

namespace chronon3d::graph {

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

    SoftwareRenderer* renderer{nullptr};
    cache::NodeCache* node_cache{nullptr};
    RenderProfiler* profiler{nullptr};
    const CompositionRegistry* registry{nullptr};
    video::VideoFrameDecoder* video_decoder{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
    bool modular_coordinates{false};
};

class RenderGraphNode {
public:
    virtual ~RenderGraphNode() = default;

    [[nodiscard]] virtual RenderGraphNodeKind kind() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual bool cacheable() const { return true; }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>& inputs
    ) = 0;
};

} // namespace chronon3d::graph
