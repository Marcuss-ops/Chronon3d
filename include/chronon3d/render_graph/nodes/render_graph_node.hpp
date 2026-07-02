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
#include <functional>
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
    /// Every node MUST be constructed with an explicit cache policy.
    /// The policy is immutable after construction — changing it requires a
    /// graph rebuild (see SourceNode::refresh() for the warning path).
    explicit RenderGraphNode(RenderNodeCachePolicy p = frame_variant_cache("default"))
        : m_cache_policy(p) {}

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

    /// Callback that evaluates the owning layer's animated opacity at a given frame.
    /// Set by the graph builder via set_opacity_evaluator().  Defaults to an empty
    /// callable; evaluate_opacity() returns 1.0f when unset.
    using OpacityEvaluator = std::function<float(const RenderFrameInfo&)>;

    void set_opacity_evaluator(OpacityEvaluator eval) { m_opacity_eval = std::move(eval); }

    [[nodiscard]] float evaluate_opacity(const RenderFrameInfo& info) const {
        return m_opacity_eval ? m_opacity_eval(info) : 1.0f;
    }

    /// Returns true when the node can serve as a fully opaque full-frame seed
    /// for the first layer in a composition.  Lets the builder skip the
    /// initial clear/composite pass for static full-frame backgrounds.
    [[nodiscard]] virtual bool can_seed_full_frame(const RenderGraphContext&) const noexcept {
        return false;
    }

    /// Immutable cache descriptor set at construction time.
    /// Subclasses must NOT override this — pass the policy to the base-class
    /// constructor instead.  Changing cache policy requires a graph rebuild.
    [[nodiscard]] RenderNodeCachePolicy cache_policy() const noexcept {
        return m_cache_policy;
    }

    [[nodiscard]] virtual cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const = 0;

    virtual NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) = 0;

protected:
    /// Cache policy is immutable after construction.  Subclasses may read
    /// it directly (e.g. cache_key() checks frame_dependent()).
    /// The base-class cache_policy() accessor is the canonical read path.
    RenderNodeCachePolicy m_cache_policy{frame_variant_cache("default")};

private:
    std::string m_layer_id;
    OpacityEvaluator m_opacity_eval;
};

} // namespace chronon3d::graph
