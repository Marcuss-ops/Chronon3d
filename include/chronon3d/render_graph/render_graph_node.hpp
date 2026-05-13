#pragma once

#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/renderer/software/framebuffer.hpp>
#include <chronon3d/scene/camera.hpp>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
    class SoftwareRenderer;
    class CompositionRegistry;
}

namespace chronon3d::video {
    class VideoDecoder;
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
    Output
};

struct RenderGraphContext {
    Frame frame{0};
    float time_seconds{0.0f};
    int width{0};
    int height{0};
    Camera camera{};

    SoftwareRenderer* renderer{nullptr};
    cache::NodeCache* node_cache{nullptr};
    RenderProfiler* profiler{nullptr};
    const CompositionRegistry* registry{nullptr};
    video::VideoDecoder* video_decoder{nullptr};

    bool cache_enabled{true};
    bool diagnostics_enabled{false};
    float ssaa_factor{1.0f};
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
