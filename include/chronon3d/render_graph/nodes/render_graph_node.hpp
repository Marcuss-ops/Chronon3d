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

    virtual OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) = 0;

private:
    std::string m_layer_id;
    bool m_frame_dependent{true};
};

} // namespace chronon3d::graph
