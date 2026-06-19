#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/core/cache_policy.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
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
    class MediaFrameProvider;
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
    Output,
    Transition,
    TextRun
};

[[nodiscard]] inline std::string_view to_string(RenderGraphNodeKind kind) {
    return enum_utils::enum_name_exact(kind);
}


class RenderGraphNode {
public:
    /// Default cache policy: frame_variant_cache (per-frame, frame-dependent).
    /// Subclasses and builder passes can override via `set_cache_policy()`
    /// before the node is observed by the executor.  Policy is immutable
    /// once the node enters the executor — `set_cache_policy()` is only
    /// legal in the build phase.
    RenderGraphNode()
        : m_cache_policy(frame_variant_cache()) {}

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
    
    virtual RenderGraphNodeKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    [[nodiscard]] std::string_view layer_id() const noexcept { return m_layer_id; }
    void set_layer_id(std::string id) { m_layer_id = std::move(id); }

    [[nodiscard]] virtual bool cacheable() const noexcept { return true; }

    /// Returns true when the node can serve as a fully opaque full-frame seed
    /// for the first layer in a composition. This lets the builder skip the
    /// initial clear/composite pass for static full-frame backgrounds.
    [[nodiscard]] virtual bool can_seed_full_frame(const RenderGraphContext&) const noexcept {
        return false;
    }

    /// Canonical cache descriptor.  The GraphExecutor reads ONLY this method.
    [[nodiscard]] const RenderNodeCachePolicy& cache_policy() const noexcept {
        return m_cache_policy;
    }

    /// Mutator used by builder passes to declare the static/animated nature
    /// of a node immediately after graph construction.  Only legal during
    /// the build phase — once the node has been observed by the executor,
    /// the policy is treated as immutable.
    void set_cache_policy(RenderNodeCachePolicy policy) noexcept {
        m_cache_policy = std::move(policy);
    }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) = 0;

private:
    std::string m_layer_id;
    RenderNodeCachePolicy m_cache_policy;
};

} // namespace chronon3d::graph
